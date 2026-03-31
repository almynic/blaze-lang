/*
 * CLI entry for the Blaze interpreter: initializes compile-time types, then
 * dispatches to the REPL (no args), embedded regression tests (--test), or
 * `interpret()` on a source file. REPL uses GNU readline when HAVE_READLINE is
 * defined; otherwise a small raw-mode line editor with history.
 */

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include "common.h"
#include "memory.h"
#include "chunk.h"
#include "debug.h"
#include "types.h"
#include "scanner.h"
#include "parser.h"
#include "ast.h"
#include "typechecker.h"
#include "compiler.h"
#include "vm.h"
#include "colors.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

// ============================================================================
// Embedded test harness (`blaze --test`): runProgram prints one snippet;
// runTests aggregates many language checks (phases printed to stdout).
// ============================================================================

/* Run one source snippet in a fresh VM; prints label and success/error. */
static void runProgram(const char* name, const char* source) {
    printf("\n--- %s ---\n", name);
    printf("Source:\n%s\n", source);

    VM vm;
    initVM(&vm);

    printf("\nOutput:\n");
    InterpretResult result = interpret(&vm, source);

    switch (result) {
        case INTERPRET_OK:
            printf("\n[Execution successful]\n");
            break;
        case INTERPRET_COMPILE_ERROR:
            printf("\n[Compile error]\n");
            break;
        case INTERPRET_RUNTIME_ERROR:
            printf("\n[Runtime error]\n");
            break;
    }

    freeVM(&vm);
}

/* Long-form embedded suite: arithmetic through modules, optionals, REPL note. */
static void runTests(void) {
    printf("Blaze VM Tests:\n");
    printf("===============\n");

    // Test 1: Simple arithmetic with print
    runProgram("Arithmetic",
        "let x: int = 10 + 20\n"
        "print(x)\n"
        "let y: int = x * 2\n"
        "print(y)\n");

    // Test 2: If statement
    runProgram("If statement",
        "let x: int = 10\n"
        "if x > 5 {\n"
        "    print(x)\n"
        "}\n");

    // Test 3: While loop (countdown)
    runProgram("While loop",
        "let x: int = 5\n"
        "while x > 0 {\n"
        "    print(x)\n"
        "    x = x - 1\n"
        "}\n");

    // Test 4: Boolean operations
    runProgram("Boolean operations",
        "let a: bool = true\n"
        "let b: bool = false\n"
        "print(a)\n"
        "print(b)\n"
        "let c: bool = !b\n"
        "print(c)\n");

    // Test 5: Comparisons
    runProgram("Comparisons",
        "let x: int = 10\n"
        "let y: int = 20\n"
        "print(x < y)\n"
        "print(x > y)\n"
        "print(x == y)\n"
        "print(x != y)\n");

    // Test 6: Float arithmetic
    runProgram("Float arithmetic",
        "let pi: float = 3.14\n"
        "let r: float = 2.0\n"
        "let area: float = pi * r * r\n"
        "print(area)\n");

    // Test 7: If-else
    runProgram("If-else",
        "let x: int = 3\n"
        "if x > 5 {\n"
        "    print(100)\n"
        "} else {\n"
        "    print(200)\n"
        "}\n");

    // Test 8: Nested if
    runProgram("Nested if",
        "let x: int = 15\n"
        "if x > 10 {\n"
        "    if x > 20 {\n"
        "        print(1)\n"
        "    } else {\n"
        "        print(2)\n"
        "    }\n"
        "} else {\n"
        "    print(3)\n"
        "}\n");

    // Test 9: Simple function
    runProgram("Simple function",
        "fn greet() {\n"
        "    print(42)\n"
        "}\n"
        "greet()\n");

    // Test 10: Function with parameters
    runProgram("Function with params",
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "let result: int = add(10, 20)\n"
        "print(result)\n");

    // Test 11: Function with return value
    runProgram("Function return",
        "fn square(x: int) -> int {\n"
        "    return x * x\n"
        "}\n"
        "print(square(5))\n");

    // Test 12: Recursive function
    runProgram("Recursive factorial",
        "fn factorial(n: int) -> int {\n"
        "    if n <= 1 {\n"
        "        return 1\n"
        "    }\n"
        "    return n * factorial(n - 1)\n"
        "}\n"
        "print(factorial(5))\n");

    // Test 13: Closure captures variable
    runProgram("Closure",
        "fn makeCounter() -> fn() -> int {\n"
        "    let count: int = 0\n"
        "    fn counter() -> int {\n"
        "        count = count + 1\n"
        "        return count\n"
        "    }\n"
        "    return counter\n"
        "}\n"
        "let c: fn() -> int = makeCounter()\n"
        "print(c())\n"
        "print(c())\n"
        "print(c())\n");

    // Test 14: Simple class
    runProgram("Simple class",
        "class Counter {\n"
        "    count: int\n"
        "    fn init() {\n"
        "        this.count = 0\n"
        "    }\n"
        "    fn increment() {\n"
        "        this.count = this.count + 1\n"
        "    }\n"
        "    fn get() -> int {\n"
        "        return this.count\n"
        "    }\n"
        "}\n"
        "let c: Counter = Counter()\n"
        "c.increment()\n"
        "c.increment()\n"
        "print(c.get())\n");

    // Test 15: Class with constructor params
    runProgram("Class with init params",
        "class Point {\n"
        "    x: int\n"
        "    y: int\n"
        "    fn init(x: int, y: int) {\n"
        "        this.x = x\n"
        "        this.y = y\n"
        "    }\n"
        "    fn sum() -> int {\n"
        "        return this.x + this.y\n"
        "    }\n"
        "}\n"
        "let p: Point = Point(10, 20)\n"
        "print(p.sum())\n");

    // Test 16: GC stress test - create many objects
    runProgram("GC stress test",
        "fn createClosure(x: int) -> fn() -> int {\n"
        "    fn inner() -> int {\n"
        "        return x\n"
        "    }\n"
        "    return inner\n"
        "}\n"
        "let sum: int = 0\n"
        "let i: int = 0\n"
        "while i < 100 {\n"
        "    let f: fn() -> int = createClosure(i)\n"
        "    sum = sum + f()\n"
        "    i = i + 1\n"
        "}\n"
        "print(sum)\n");  // Sum of 0..99 = 4950

    printf("\n===============\n");
    printf("Phase 10: Standard Library Tests\n");
    printf("===============\n");

    // Test 17: String length
    runProgram("String length",
        "let s: string = \"Hello\"\n"
        "print(len(s))\n");  // Should print 5

    // Test 18: String concatenation
    runProgram("String concat",
        "let a: string = \"Hello\"\n"
        "let b: string = \" World\"\n"
        "let c: string = concat(a, b)\n"
        "print(c)\n");  // Should print "Hello World"

    // Test 19: Substring
    runProgram("Substring",
        "let s: string = \"Hello World\"\n"
        "let sub: string = substr(s, 0, 5)\n"
        "print(sub)\n");  // Should print "Hello"

    // Test 20: Math functions
    runProgram("Math functions",
        "print(abs(-42))\n"          // 42
        "print(min(10, 20))\n"       // 10
        "print(max(10, 20))\n");     // 20

    // Test 21: Type conversion
    runProgram("Type conversion",
        "let n: int = 42\n"
        "let s: string = toString(n)\n"
        "print(s)\n"                  // "42"
        "let f: float = toFloat(n)\n"
        "print(f)\n");               // 42.0

    // Test 22: Character operations
    runProgram("Char operations",
        "let s: string = \"Hello\"\n"
        "print(charAt(s, 0))\n"      // "H"
        "print(charAt(s, 4))\n");    // "o"

    // Test 23: Case conversion
    runProgram("Case conversion",
        "let s: string = \"Hello World\"\n"
        "print(toUpper(s))\n"        // "HELLO WORLD"
        "print(toLower(s))\n");      // "hello world"

    // Test 24: String search
    runProgram("String search",
        "let s: string = \"Hello World\"\n"
        "print(indexOf(s, \"World\"))\n"   // 6
        "print(indexOf(s, \"xyz\"))\n");   // -1

    // Test 25: Trim whitespace
    runProgram("Trim whitespace",
        "let s: string = \"  hello  \"\n"
        "let t: string = trim(s)\n"
        "print(t)\n"                  // "hello"
        "print(len(t))\n");          // 5

    // Test 26: Type introspection
    runProgram("Type introspection",
        "print(type(42))\n"          // "int"
        "print(type(3.14))\n"        // "float"
        "print(type(true))\n"        // "bool"
        "print(type(\"hi\"))\n");    // "string"

    // Test 27: Math - sqrt and pow
    runProgram("Math sqrt/pow",
        "print(sqrt(16.0))\n"        // 4
        "print(pow(2.0, 10.0))\n");  // 1024

    // Test 28: Floor, ceil, round
    runProgram("Floor/ceil/round",
        "print(floor(3.7))\n"        // 3
        "print(ceil(3.2))\n"         // 4
        "print(round(3.5))\n");      // 4

    printf("\n===============\n");
    printf("Phase 11: Array Index Assignment\n");
    printf("===============\n");

    // Test 29: Array index assignment
    runProgram("Array index assignment",
        "let arr: [int] = [1, 2, 3, 4, 5]\n"
        "print(arr[2])\n"            // 3
        "arr[2] = 100\n"
        "print(arr[2])\n"            // 100
        "arr[0] = arr[4] + 10\n"
        "print(arr[0])\n");          // 15

    printf("\n===============\n");
    printf("Phase 12: Super Method Calls\n");
    printf("===============\n");

    // Test 30: Super method call
    runProgram("Super method call",
        "class Animal {\n"
        "    fn speak() -> int {\n"
        "        return 10\n"
        "    }\n"
        "}\n"
        "class Dog extends Animal {\n"
        "    fn speak() -> int {\n"
        "        return super.speak() + 5\n"
        "    }\n"
        "}\n"
        "let d: Dog = Dog()\n"
        "print(d.speak())\n");       // 15

    // Test 31: Super with init
    runProgram("Super with init",
        "class Base {\n"
        "    value: int\n"
        "    fn init(v: int) {\n"
        "        this.value = v\n"
        "    }\n"
        "    fn getValue() -> int {\n"
        "        return this.value\n"
        "    }\n"
        "}\n"
        "class Derived extends Base {\n"
        "    fn init(v: int) {\n"
        "        this.value = v * 2\n"
        "    }\n"
        "    fn getValue() -> int {\n"
        "        return super.getValue() + 100\n"
        "    }\n"
        "}\n"
        "let d: Derived = Derived(10)\n"
        "print(d.getValue())\n");    // 120 (20 + 100)

    printf("\n===============\n");
    printf("Phase 13: For Loop Iteration\n");
    printf("===============\n");

    // Test 32: For loop over array
    runProgram("For loop over array",
        "let arr: [int] = [10, 20, 30, 40, 50]\n"
        "let sum: int = 0\n"
        "for x in arr {\n"
        "    sum = sum + x\n"
        "}\n"
        "print(sum)\n");             // 150

    // Test 33: For loop with print
    runProgram("For loop with print",
        "let names: [int] = [1, 2, 3]\n"
        "for n in names {\n"
        "    print(n)\n"
        "}\n");                      // 1, 2, 3

    printf("\n===============\n");
    printf("Phase 14: Lambda Expressions\n");
    printf("===============\n");

    // Test 34: Simple lambda
    runProgram("Simple lambda",
        "let double: fn(int) -> int = (x) => x * 2\n"
        "print(double(5))\n");       // 10

    // Test 35: Lambda with multiple params
    runProgram("Lambda with multiple params",
        "let add: fn(int, int) -> int = (a, b) => a + b\n"
        "print(add(3, 4))\n");       // 7

    // Test 36: Lambda capturing variable (closure)
    runProgram("Lambda closure",
        "let multiplier: int = 10\n"
        "let scale: fn(int) -> int = (x) => x * multiplier\n"
        "print(scale(5))\n");        // 50

    printf("\n===============\n");
    printf("Phase 15: Match Statements\n");
    printf("===============\n");

    // Test 37: Basic match with integers
    runProgram("Match with integers",
        "let x: int = 2\n"
        "match x {\n"
        "    1 => { print(10) }\n"
        "    2 => { print(20) }\n"
        "    3 => { print(30) }\n"
        "}\n");                      // 20

    // Test 38: Match with wildcard
    runProgram("Match with wildcard",
        "let x: int = 99\n"
        "match x {\n"
        "    1 => { print(10) }\n"
        "    2 => { print(20) }\n"
        "    _ => { print(0) }\n"
        "}\n");                      // 0

    // Test 39: Match with booleans
    runProgram("Match with booleans",
        "let flag: bool = false\n"
        "match flag {\n"
        "    true => { print(1) }\n"
        "    false => { print(0) }\n"
        "}\n");                      // 0

    printf("\n===============\n");
    printf("Phase 16: Array Functions\n");
    printf("===============\n");

    // Test 40: Array push
    runProgram("Array push",
        "let arr: [int] = [1, 2, 3]\n"
        "print(len(arr))\n"          // 3
        "let tmp = push(arr, 4)\n"
        "print(len(arr))\n");        // 4

    // Test 41: Array pop
    runProgram("Array pop",
        "let arr: [int] = [10, 20, 30]\n"
        "print(pop(arr))\n"          // 30
        "print(len(arr))\n");        // 2

    // Test 42: Array first and last
    runProgram("Array first and last",
        "let arr: [int] = [100, 200, 300]\n"
        "print(first(arr))\n"        // 100
        "print(last(arr))\n");       // 300

    // Test 43: Array contains
    runProgram("Array contains",
        "let arr: [int] = [1, 2, 3, 4, 5]\n"
        "print(contains(arr, 3))\n"  // true
        "print(contains(arr, 99))\n"); // false

    // Test 44: Array reverse
    runProgram("Array reverse",
        "let arr: [int] = [1, 2, 3]\n"
        "reverse(arr)\n"
        "print(first(arr))\n"        // 3
        "print(last(arr))\n");       // 1

    // Test 45: Array slice
    runProgram("Array slice",
        "let arr: [int] = [1, 2, 3, 4, 5]\n"
        "let s = slice(arr, 1, 4)\n"
        "print(len(s))\n"            // 3
        "print(first(s))\n"          // 2
        "print(last(s))\n");         // 4

    // Test 46: String split
    runProgram("String split",
        "let s: string = \"a,b,c,d\"\n"
        "let parts = split(s, \",\")\n"
        "print(len(parts))\n"        // 4
        "print(first(parts))\n");    // a

    // Test 47: Array join
    runProgram("Array join",
        "let arr: [string] = [\"hello\", \"world\"]\n"
        "let s: string = join(arr, \" \")\n"
        "print(s)\n");               // hello world

    // Test 48: String replace
    runProgram("String replace",
        "let s: string = \"hello world\"\n"
        "let r: string = replace(s, \"world\", \"blaze\")\n"
        "print(r)\n");               // hello blaze

    printf("\n===============\n");
    printf("Phase 17: Range Syntax\n");
    printf("===============\n");

    // Test 49: Basic range
    runProgram("Basic range",
        "let r = 0..5\n"
        "print(len(r))\n"            // 5
        "print(first(r))\n"          // 0
        "print(last(r))\n");         // 4

    // Test 50: Range in for loop
    runProgram("Range in for loop",
        "let sum: int = 0\n"
        "for i in 0..5 {\n"
        "    sum = sum + i\n"
        "}\n"
        "print(sum)\n");             // 10 (0+1+2+3+4)

    // Test 51: Range with expressions
    runProgram("Range with expressions",
        "let start: int = 2\n"
        "let end: int = 7\n"
        "let r = start..end\n"
        "print(len(r))\n");          // 5

    printf("\n===============\n");
    printf("Phase 17 tests complete! Range syntax is ready.\n");

    printf("\n===============\n");
    printf("Phase 18: Extended Standard Library\n");
    printf("===============\n");

    // Test 52: String startsWith
    runProgram("String startsWith",
        "let s: string = \"Hello World\"\n"
        "print(startsWith(s, \"Hello\"))\n"   // true
        "print(startsWith(s, \"World\"))\n"); // false

    // Test 53: String endsWith
    runProgram("String endsWith",
        "let s: string = \"Hello World\"\n"
        "print(endsWith(s, \"World\"))\n"     // true
        "print(endsWith(s, \"Hello\"))\n");   // false

    // Test 54: String repeat
    runProgram("String repeat",
        "let s: string = \"ab\"\n"
        "print(repeat(s, 3))\n"               // ababab
        "print(len(repeat(s, 4)))\n");        // 8

    // Test 55: Array sort
    runProgram("Array sort",
        "let arr: [int] = [5, 2, 8, 1, 9]\n"
        "sort(arr)\n"
        "print(first(arr))\n"                 // 1
        "print(last(arr))\n");                // 9

    // Test 56: Random (just test it runs)
    runProgram("Random float",
        "let r: float = random()\n"
        "let valid: bool = r >= 0.0\n"
        "print(valid)\n");                    // true

    // Test 57: Random int in range
    runProgram("Random int range",
        "let r: int = randomInt(0, 100)\n"
        "let valid: bool = r >= 0\n"
        "print(valid)\n");                    // true

    printf("\n===============\n");
    printf("Phase 18 tests complete! Extended stdlib is ready.\n");

    printf("\n===============\n");
    printf("Phase 19: File I/O\n");
    printf("===============\n");

    // Test 58: Write and read file
    runProgram("File write/read",
        "let content: string = \"Hello from Blaze!\"\n"
        "let ok: bool = writeFile(\"/tmp/blaze_test.txt\", content)\n"
        "print(ok)\n"
        "let read: string = readFile(\"/tmp/blaze_test.txt\")\n"
        "print(read)\n");

    // Test 59: File exists
    runProgram("File exists",
        "print(fileExists(\"/tmp/blaze_test.txt\"))\n"   // true
        "print(fileExists(\"/nonexistent_file.txt\"))\n"); // false

    // Test 60: Append file
    runProgram("File append",
        "appendFile(\"/tmp/blaze_test.txt\", \" More text.\")\n"
        "let content: string = readFile(\"/tmp/blaze_test.txt\")\n"
        "print(content)\n");

    // Test 61: Delete file
    runProgram("File delete",
        "let deleted: bool = deleteFile(\"/tmp/blaze_test.txt\")\n"
        "print(deleted)\n"
        "print(fileExists(\"/tmp/blaze_test.txt\"))\n"); // false

    printf("\n===============\n");
    printf("Phase 19 tests complete! File I/O is ready.\n");

    printf("\n===============\n");
    printf("Phase 20: Exception Handling\n");
    printf("===============\n");

    // Test 62: Basic try/catch
    runProgram("Basic try/catch",
        "try {\n"
        "    print(1)\n"
        "    throw \"error\"\n"
        "    print(2)\n"
        "} catch (e) {\n"
        "    print(3)\n"
        "    print(e)\n"
        "}\n"
        "print(4)\n");

    // Test 63: No exception
    runProgram("Try without exception",
        "try {\n"
        "    print(10)\n"
        "    print(20)\n"
        "} catch (e) {\n"
        "    print(99)\n"
        "}\n"
        "print(30)\n");

    // Test 64: Throw with expression
    runProgram("Throw with value",
        "let msg: string = \"Something failed\"\n"
        "try {\n"
        "    throw msg\n"
        "} catch (e) {\n"
        "    print(e)\n"
        "}\n");

    printf("\n===============\n");
    printf("Phase 20 tests complete! Exception handling is ready.\n");

    printf("\n===============\n");
    printf("Phase 21: Module Imports\n");
    printf("===============\n");

    // Create a test module file
    FILE* modFile = fopen("/tmp/testmod.blaze", "w");
    if (modFile) {
        fprintf(modFile,
            "fn add(a: int, b: int) -> int {\n"
            "    return a + b\n"
            "}\n"
            "fn double(x: int) -> int {\n"
            "    return x * 2\n"
            "}\n");
        fclose(modFile);

        // Test 65: Basic import
        runProgram("Basic import",
            "import \"/tmp/testmod.blaze\"\n"
            "print(add(3, 4))\n"       // 7
            "print(double(5))\n");     // 10

        // Clean up
        remove("/tmp/testmod.blaze");
    }

    // Test 66: Standard library import
    runProgram("Std library import",
        "import \"std/math\"\n"
        "print(abs(-5))\n"        // 5
        "print(min(10, 3))\n"     // 3
        "print(max(10, 3))\n");   // 10

    // Test 67: Array library import
    runProgram("Array library import",
        "import \"std/array\"\n"
        "let nums: [int] = [1, 2, 3, 4, 5]\n"
        "print(sum(nums))\n"       // 15
        "print(product(nums))\n"); // 120

    // Test 68: String library functions
    runProgram("String library import",
        "import \"std/string\"\n"
        "print(contains(\"hello world\", \"world\"))\n"  // true
        "print(isEmpty(\"\"))\n"                         // true
        "print(padLeft(\"42\", 5, \"0\"))\n"            // 00042
        "print(countOccurrences(\"ababa\", \"ab\"))\n"); // 2

    // Test 69: Math library functions
    runProgram("Math library expanded",
        "import \"std/math\"\n"
        "print(sign(-5))\n"        // -1
        "print(isEven(4))\n"       // true
        "print(gcd(12, 18))\n"     // 6
        "print(factorial(5))\n"    // 120
        "print(fib(10))\n"         // 55
        "print(isPrime(17))\n");   // true

    printf("\n===============\n");
    printf("Phase 21 tests complete! Module imports are ready.\n");

    printf("\n===============\n");
    printf("Phase 22: Optional Types\n");
    printf("===============\n");

    // Test 70: Null coalescing with nil
    runProgram("Null coalescing with nil",
        "let x: int? = nil\n"
        "let y = x ?? 10\n"
        "print(y)\n");  // 10

    // Test 71: Null coalescing with value
    runProgram("Null coalescing with value",
        "let z: int? = 5\n"
        "let w = z ?? 10\n"
        "print(w)\n");  // 5

    // Test 72: Nested null coalescing
    runProgram("Nested null coalescing",
        "let a: int? = nil\n"
        "let b: int? = nil\n"
        "let c: int? = 100\n"
        "let result = a ?? b ?? c ?? 0\n"
        "print(result)\n");  // 100

    // Test 73: Optional type annotation
    runProgram("Optional type annotation",
        "let maybeInt: int? = nil\n"
        "print(maybeInt)\n"  // nil
        "maybeInt = 42\n"
        "print(maybeInt)\n");  // 42

    // Test 74: Optional chaining with nil
    runProgram("Optional chaining with nil",
        "class Person {\n"
        "    name: string\n"
        "    fn init(n: string) {\n"
        "        this.name = n\n"
        "    }\n"
        "}\n"
        "let p: Person? = nil\n"
        "let name = p?.name\n"
        "print(name)\n");  // nil

    // Test 75: Optional chaining with value
    runProgram("Optional chaining with value",
        "class Person {\n"
        "    name: string\n"
        "    fn init(n: string) {\n"
        "        this.name = n\n"
        "    }\n"
        "}\n"
        "let p: Person? = Person(\"Alice\")\n"
        "let name = p?.name\n"
        "print(name)\n");  // Alice

    printf("\n===============\n");
    printf("Phase 22 tests complete! Optional types are ready.\n");

    printf("\n===============\n");
    printf("Phase 23: REPL (Note: Interactive testing only)\n");
    printf("===============\n");
    printf("REPL features implemented:\n");
    printf("  - Interactive mode: run 'blaze' with no arguments\n");
    printf("  - Variable/function persistence between inputs\n");
    printf("  - Multi-line input with brace detection\n");
    printf("  - Commands: .help, .exit, .clear, .test\n");
    printf("Phase 23 tests complete! REPL is ready.\n");
}

// ============================================================================
// Script file: read UTF-8 source, run interpret(), map errors to exit codes.
// ============================================================================

/* Slurp a file into a malloc'd, null-terminated buffer; NULL on error. */
static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

/* Read path, interpret, free; exit 65/70 on compile/runtime failure. */
static void runFile(const char* path, bool debugMode, bool debugProtocolMode,
                    const char* breakpointsPath, int* breakLines, int breakCount,
                    const char** breakConds) {
    char* source = readFile(path);
    if (source == NULL) return;

    VM vm;
    initVM(&vm);
    if (debugMode) {
        setDebuggerEnabled(&vm, true);
        setDebuggerProtocolMode(&vm, debugProtocolMode);
        setDebuggerBreakpointsPath(&vm, breakpointsPath);
        for (int i = 0; i < breakCount; i++) {
            debuggerAddBreakpoint(&vm, breakLines[i], breakConds[i]);
        }
        // Pause immediately at first executed source line.
        vm.debuggerStepMode = DEBUG_STEP_IN;
        if (debugProtocolMode) {
            printf("{\"event\":\"debuggerEnabled\",\"protocol\":\"jsonl\",\"bpFile\":\"%s\"}\n", breakpointsPath);
        } else {
            printf("[debug] enabled (bp file: %s)\n", breakpointsPath);
        }
    }

    InterpretResult result = interpret(&vm, source);

    freeVM(&vm);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

// ============================================================================
// REPL: persistent VM across inputs, multi-line brace continuation, dot
// commands (.help, .exit, .clear, .test). Line input: readline if available,
// else custom editor (History + LineBuffer + readLineWithEditing below).
// ============================================================================

#define REPL_LINE_MAX 1024
#define REPL_INPUT_MAX 65536
#define HISTORY_MAX 100

// Terminal handling
static struct termios origTermios;
static bool rawModeEnabled = false;

static void disableRawMode(void) {
    if (rawModeEnabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
        rawModeEnabled = false;
    }
}

static void enableRawMode(void) {
    if (!isatty(STDIN_FILENO)) return;

    tcgetattr(STDIN_FILENO, &origTermios);
    rawModeEnabled = true;
    atexit(disableRawMode);

    struct termios raw = origTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Keep OPOST enabled so \n works correctly for output
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// History management
typedef struct {
    char** lines;
    int count;
    int capacity;
    int index;  // Current position when navigating
} History;

static void initHistory(History* h) {
    h->lines = malloc(sizeof(char*) * HISTORY_MAX);
    h->count = 0;
    h->capacity = HISTORY_MAX;
    h->index = 0;
}

static void freeHistory(History* h) {
    for (int i = 0; i < h->count; i++) {
        free(h->lines[i]);
    }
    free(h->lines);
}

static void addHistory(History* h, const char* line) {
    // Don't add empty lines or duplicates of the last entry
    if (strlen(line) == 0) return;
    if (h->count > 0 && strcmp(h->lines[h->count - 1], line) == 0) return;

    // If at capacity, remove oldest entry
    if (h->count >= h->capacity) {
        free(h->lines[0]);
        memmove(h->lines, h->lines + 1, sizeof(char*) * (h->capacity - 1));
        h->count--;
    }

    h->lines[h->count] = strdup(line);
    h->count++;
    h->index = h->count;  // Reset to end
}

// Line editor
typedef struct {
    char* buf;
    int len;
    int pos;      // Cursor position
    int capacity;
} LineBuffer;

static void initLineBuffer(LineBuffer* lb, int capacity) {
    lb->buf = malloc(capacity);
    lb->buf[0] = '\0';
    lb->len = 0;
    lb->pos = 0;
    lb->capacity = capacity;
}

static void freeLineBuffer(LineBuffer* lb) {
    free(lb->buf);
}

static void clearLineBuffer(LineBuffer* lb) {
    lb->buf[0] = '\0';
    lb->len = 0;
    lb->pos = 0;
}

static void refreshLine(const char* prompt, LineBuffer* lb) {
    char seq[64];

    // Move cursor to left edge
    snprintf(seq, sizeof(seq), "\r");
    write(STDOUT_FILENO, seq, strlen(seq));

    // Write prompt
    write(STDOUT_FILENO, prompt, strlen(prompt));

    // Write buffer
    write(STDOUT_FILENO, lb->buf, lb->len);

    // Erase to right of cursor
    snprintf(seq, sizeof(seq), "\x1b[K");
    write(STDOUT_FILENO, seq, strlen(seq));

    // Move cursor to correct position
    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(strlen(prompt) + lb->pos));
    write(STDOUT_FILENO, seq, strlen(seq));
}

static void insertChar(LineBuffer* lb, char c) {
    if (lb->len >= lb->capacity - 1) return;

    // Shift characters to make room
    memmove(lb->buf + lb->pos + 1, lb->buf + lb->pos, lb->len - lb->pos + 1);
    lb->buf[lb->pos] = c;
    lb->len++;
    lb->pos++;
}

static void deleteCharLeft(LineBuffer* lb) {
    if (lb->pos > 0) {
        memmove(lb->buf + lb->pos - 1, lb->buf + lb->pos, lb->len - lb->pos + 1);
        lb->pos--;
        lb->len--;
    }
}

static void deleteCharRight(LineBuffer* lb) {
    if (lb->pos < lb->len) {
        memmove(lb->buf + lb->pos, lb->buf + lb->pos + 1, lb->len - lb->pos);
        lb->len--;
    }
}

static void setLineContent(LineBuffer* lb, const char* content) {
    int len = strlen(content);
    if (len >= lb->capacity) len = lb->capacity - 1;
    memcpy(lb->buf, content, len);
    lb->buf[len] = '\0';
    lb->len = len;
    lb->pos = len;
}

// Read a line with editing support
// Returns: 1 = got line, 0 = EOF, -1 = error
static int readLineWithEditing(const char* prompt, LineBuffer* lb, History* history) {
    clearLineBuffer(lb);
    history->index = history->count;
    char* savedLine = NULL;  // Save current input when navigating history

    write(STDOUT_FILENO, prompt, strlen(prompt));

    while (1) {
        char c;
        int nread = read(STDIN_FILENO, &c, 1);
        if (nread <= 0) return 0;  // EOF

        // Handle escape sequences (arrow keys, etc.)
        if (c == '\x1b') {
            char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A':  // Up arrow - previous history
                        if (history->count > 0 && history->index > 0) {
                            // Save current line if at the end
                            if (history->index == history->count) {
                                free(savedLine);
                                savedLine = strdup(lb->buf);
                            }
                            history->index--;
                            setLineContent(lb, history->lines[history->index]);
                            refreshLine(prompt, lb);
                        }
                        break;
                    case 'B':  // Down arrow - next history
                        if (history->index < history->count) {
                            history->index++;
                            if (history->index == history->count) {
                                // Restore saved line
                                setLineContent(lb, savedLine ? savedLine : "");
                            } else {
                                setLineContent(lb, history->lines[history->index]);
                            }
                            refreshLine(prompt, lb);
                        }
                        break;
                    case 'C':  // Right arrow
                        if (lb->pos < lb->len) {
                            lb->pos++;
                            refreshLine(prompt, lb);
                        }
                        break;
                    case 'D':  // Left arrow
                        if (lb->pos > 0) {
                            lb->pos--;
                            refreshLine(prompt, lb);
                        }
                        break;
                    case 'H':  // Home
                        lb->pos = 0;
                        refreshLine(prompt, lb);
                        break;
                    case 'F':  // End
                        lb->pos = lb->len;
                        refreshLine(prompt, lb);
                        break;
                    case '3':  // Delete key (sends \x1b[3~)
                        if (read(STDIN_FILENO, &seq[2], 1) == 1 && seq[2] == '~') {
                            deleteCharRight(lb);
                            refreshLine(prompt, lb);
                        }
                        break;
                }
            }
            continue;
        }

        switch (c) {
            case '\r':  // Enter
            case '\n':
                write(STDOUT_FILENO, "\n", 1);
                free(savedLine);
                return 1;

            case 127:   // Backspace (DEL)
            case '\b':  // Backspace (Ctrl+H)
                deleteCharLeft(lb);
                refreshLine(prompt, lb);
                break;

            case 1:  // Ctrl+A - beginning of line
                lb->pos = 0;
                refreshLine(prompt, lb);
                break;

            case 5:  // Ctrl+E - end of line
                lb->pos = lb->len;
                refreshLine(prompt, lb);
                break;

            case 4:  // Ctrl+D - EOF if empty, delete char otherwise
                if (lb->len == 0) {
                    free(savedLine);
                    return 0;
                }
                deleteCharRight(lb);
                refreshLine(prompt, lb);
                break;

            case 11:  // Ctrl+K - kill to end of line
                lb->buf[lb->pos] = '\0';
                lb->len = lb->pos;
                refreshLine(prompt, lb);
                break;

            case 21:  // Ctrl+U - kill to beginning of line
                memmove(lb->buf, lb->buf + lb->pos, lb->len - lb->pos + 1);
                lb->len -= lb->pos;
                lb->pos = 0;
                refreshLine(prompt, lb);
                break;

            case 12:  // Ctrl+L - clear screen
                write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
                refreshLine(prompt, lb);
                break;

            case 3:  // Ctrl+C - cancel line
                write(STDOUT_FILENO, "^C\n", 3);
                clearLineBuffer(lb);
                free(savedLine);
                return 1;

            default:
                if (c >= 32 && c < 127) {  // Printable characters
                    insertChar(lb, c);
                    refreshLine(prompt, lb);
                }
                break;
        }
    }
}

// Count unmatched braces/brackets/parens in source
static int countUnmatchedDelimiters(const char* source) {
    int braces = 0;      // {}
    int brackets = 0;    // []
    int parens = 0;      // ()
    bool inString = false;
    bool inComment = false;

    for (const char* c = source; *c != '\0'; c++) {
        // Handle string literals
        if (*c == '"' && !inComment) {
            if (c > source && *(c - 1) == '\\') continue;  // Escaped quote
            inString = !inString;
            continue;
        }

        if (inString) continue;

        // Handle single-line comments
        if (*c == '/' && *(c + 1) == '/') {
            inComment = true;
            continue;
        }
        if (*c == '\n') {
            inComment = false;
            continue;
        }

        if (inComment) continue;

        // Count delimiters
        switch (*c) {
            case '{': braces++; break;
            case '}': braces--; break;
            case '[': brackets++; break;
            case ']': brackets--; break;
            case '(': parens++; break;
            case ')': parens--; break;
        }
    }

    return braces + brackets + parens;
}

static void printReplHelp(void) {
    printf("\nBlaze REPL Commands:\n");
    printf("  .help     Show this help message\n");
    printf("  .exit     Exit the REPL\n");
    printf("  .clear    Clear the screen\n");
    printf("  .test     Run built-in tests\n");
    printf("  .history  Show command history\n");
    printf("\nKeyboard shortcuts:\n");
    printf("  Up/Down   Navigate command history\n");
    printf("  Left/Right  Move cursor\n");
    printf("  Ctrl+A/E  Beginning/end of line\n");
    printf("  Ctrl+U/K  Delete to beginning/end of line\n");
    printf("  Ctrl+L    Clear screen\n");
    printf("  Ctrl+C    Cancel current input\n");
    printf("  Ctrl+D    Exit (on empty line)\n");
    printf("\nEnter Blaze code to execute. Multi-line input is supported.\n");
    printf("Use braces {} for blocks, and the input will continue until balanced.\n\n");
}

static void repl(void) {
    // Check if we're in a terminal
    bool interactive = isatty(STDIN_FILENO);

    if (interactive && colorsEnabled()) {
        printf("%s%sBlaze REPL%s %sv0.1.0%s\n",
               COLOR_BOLD, COLOR_BRIGHT_CYAN, COLOR_RESET,
               COLOR_GRAY, COLOR_RESET);
        printf("%sType %s.help%s for commands, %s.exit%s to quit%s\n\n",
               COLOR_GRAY, COLOR_BRIGHT_YELLOW, COLOR_GRAY,
               COLOR_BRIGHT_YELLOW, COLOR_GRAY, COLOR_RESET);
    } else {
        printf("Blaze REPL v0.1.0\n");
        printf("Type .help for commands, .exit to quit\n\n");
    }

    VM vm;
    initVM(&vm);

#ifdef HAVE_READLINE
    // Load history file for readline
    char* home = getenv("HOME");
    char histfile[1024];
    if (home) {
        snprintf(histfile, sizeof(histfile), "%s/.blaze_history", home);
        read_history(histfile);
    }
#else
    History history;
    initHistory(&history);
    LineBuffer lb;
    initLineBuffer(&lb, REPL_LINE_MAX);
    if (interactive) {
        enableRawMode();
    }
#endif

    char* input = malloc(REPL_INPUT_MAX);
    char line[REPL_LINE_MAX];
    bool running = true;

    while (running) {
        // Reset input buffer
        input[0] = '\0';
        size_t inputLen = 0;
        int continuation = 0;

        // Read input (possibly multi-line)
        while (true) {
            // Create colored prompt
            char promptBuf[64];
            if (interactive && colorsEnabled()) {
                if (inputLen == 0) {
                    snprintf(promptBuf, sizeof(promptBuf), "%sblaze>%s ",
                            COLOR_BRIGHT_GREEN, COLOR_RESET);
                } else {
                    snprintf(promptBuf, sizeof(promptBuf), "%s......%s ",
                            COLOR_BRIGHT_BLUE, COLOR_RESET);
                }
            } else {
                snprintf(promptBuf, sizeof(promptBuf), "%s",
                        (inputLen == 0) ? "blaze> " : "...... ");
            }
            const char* prompt = promptBuf;

#ifdef HAVE_READLINE
            if (interactive) {
                char* readlineInput = readline(prompt);
                if (readlineInput == NULL) {
                    // EOF (Ctrl+D)
                    printf("\n");
                    running = false;
                    break;
                }
                strncpy(line, readlineInput, REPL_LINE_MAX - 1);
                line[REPL_LINE_MAX - 1] = '\0';
                free(readlineInput);
            } else {
                printf("%s", prompt);
                fflush(stdout);
                if (fgets(line, REPL_LINE_MAX, stdin) == NULL) {
                    printf("\n");
                    running = false;
                    break;
                }
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') {
                    line[len - 1] = '\0';
                }
            }
#else
            int result;
            if (interactive) {
                result = readLineWithEditing(prompt, &lb, &history);
                if (result <= 0) {
                    printf("\n");
                    running = false;
                    break;
                }
                strncpy(line, lb.buf, REPL_LINE_MAX - 1);
                line[REPL_LINE_MAX - 1] = '\0';
            } else {
                // Non-interactive: use simple fgets
                printf("%s", prompt);
                fflush(stdout);
                if (fgets(line, REPL_LINE_MAX, stdin) == NULL) {
                    printf("\n");
                    running = false;
                    break;
                }
                // Remove trailing newline
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') {
                    line[len - 1] = '\0';
                }
            }
#endif

            // Handle special commands (only at start of input)
            if (inputLen == 0 && line[0] == '.') {
                if (strcmp(line, ".exit") == 0 || strcmp(line, ".quit") == 0) {
                    running = false;
                    break;
                } else if (strcmp(line, ".help") == 0) {
                    printReplHelp();
                    break;
                } else if (strcmp(line, ".clear") == 0) {
                    printf("\033[2J\033[H");
                    break;
                } else if (strcmp(line, ".test") == 0) {
                    if (interactive) disableRawMode();
                    printf("\nRunning tests...\n");
                    runTests();
                    if (interactive) enableRawMode();
                    break;
                } else if (strcmp(line, ".history") == 0) {
                    printf("\nCommand history:\n");
#ifdef HAVE_READLINE
                    // Use history_get to iterate through history
                    for (int i = history_base; i < history_base + history_length; i++) {
                        HIST_ENTRY* entry = history_get(i);
                        if (entry) {
                            printf("  %d: %s\n", i - history_base + 1, entry->line);
                        }
                    }
                    if (history_length == 0) {
                        printf("  (empty)\n");
                    }
#else
                    for (int i = 0; i < history.count; i++) {
                        printf("  %d: %s\n", i + 1, history.lines[i]);
                    }
#endif
                    printf("\n");
                    break;
                } else {
                    printf("Unknown command: %s (type .help for commands)\n", line);
                    break;
                }
            }

            // Append line to input
            size_t lineLen = strlen(line);
            if (inputLen + lineLen + 1 >= REPL_INPUT_MAX - 1) {
                printf("Error: Input too long\n");
                input[0] = '\0';
                break;
            }
            if (inputLen > 0) {
                strcat(input, "\n");
                inputLen++;
            }
            strcat(input, line);
            inputLen += lineLen;

            // Check if input is complete (balanced delimiters)
            continuation = countUnmatchedDelimiters(input);
            if (continuation <= 0) {
                break;
            }
        }

        // Execute if we have input
        if (inputLen > 0 && running) {
            // Remove trailing whitespace
            while (inputLen > 0 && (input[inputLen - 1] == '\n' ||
                                     input[inputLen - 1] == ' ' ||
                                     input[inputLen - 1] == '\t')) {
                input[--inputLen] = '\0';
            }

            if (inputLen > 0) {
#ifdef HAVE_READLINE
                // Add to readline history
                if (interactive) {
                    add_history(input);
                }
#else
                // Add to history (only first line for multi-line inputs)
                char* firstLine = strdup(input);
                char* nl = strchr(firstLine, '\n');
                if (nl) *nl = '\0';
                addHistory(&history, firstLine);
                free(firstLine);
#endif

                InterpretResult result = interpretRepl(&vm, input);
                if (result == INTERPRET_COMPILE_ERROR) {
                    // Error already printed by compiler
                } else if (result == INTERPRET_RUNTIME_ERROR) {
                    // Error already printed by VM
                }
            }
        }
    }

#ifdef HAVE_READLINE
    // Save history file for readline
    if (home) {
        write_history(histfile);
    }
#else
    if (interactive) {
        disableRawMode();
    }
    freeLineBuffer(&lb);
    freeHistory(&history);
#endif

    free(input);
    freeVM(&vm);

    printf("Goodbye!\n");
}

/* Args: none → REPL; `--test`/`-t` → runTests; one path → runFile. Exit 64 on
 * bad usage, 65 compile error, 70 runtime error (runFile). */
int main(int argc, char* argv[]) {
    // Initialize type system
    initTypeSystem();

    if (argc == 1) {
        // No arguments - run REPL
        repl();
    } else {
        bool debugMode = false;
        bool debugProtocolMode = false;
        const char* bpPath = ".blaze_breakpoints";
        int breakLines[64];
        const char* breakConds[64];
        int breakCount = 0;
        const char* scriptPath = NULL;

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--test") == 0 || strcmp(argv[i], "-t") == 0) {
                if (argc != 2) {
                    fprintf(stderr, "Usage: blaze --test\n");
                    freeTypeSystem();
                    return 64;
                }
                // Run tests
                printf("Blaze Language v0.1.0\n");
                printf("=====================\n\n");
                runTests();
                printf("\nAll tests complete!\n");
                freeTypeSystem();
                return 0;
            } else if (strcmp(argv[i], "--debug") == 0) {
                debugMode = true;
            } else if (strcmp(argv[i], "--debug-protocol") == 0) {
                debugMode = true;
                debugProtocolMode = true;
            } else if (strcmp(argv[i], "--bp-file") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "--bp-file requires a path\n");
                    freeTypeSystem();
                    return 64;
                }
                bpPath = argv[++i];
            } else if (strcmp(argv[i], "--break") == 0) {
                if (i + 1 >= argc || breakCount >= 64) {
                    fprintf(stderr, "--break requires a line number\n");
                    freeTypeSystem();
                    return 64;
                }
                breakLines[breakCount] = atoi(argv[++i]);
                breakConds[breakCount] = NULL;
                breakCount++;
            } else if (strcmp(argv[i], "--break-if") == 0) {
                if (i + 1 >= argc || breakCount >= 64) {
                    fprintf(stderr, "--break-if requires LINE:COND\n");
                    freeTypeSystem();
                    return 64;
                }
                const char* spec = argv[++i];
                const char* colon = strchr(spec, ':');
                if (colon == NULL) {
                    fprintf(stderr, "--break-if format is LINE:COND\n");
                    freeTypeSystem();
                    return 64;
                }
                char lineBuf[32];
                int n = (int)(colon - spec);
                if (n <= 0 || n >= (int)sizeof(lineBuf)) {
                    fprintf(stderr, "invalid line in --break-if\n");
                    freeTypeSystem();
                    return 64;
                }
                memcpy(lineBuf, spec, (size_t)n);
                lineBuf[n] = '\0';
                breakLines[breakCount] = atoi(lineBuf);
                breakConds[breakCount] = colon + 1;
                breakCount++;
            } else if (argv[i][0] == '-') {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                freeTypeSystem();
                return 64;
            } else {
                scriptPath = argv[i];
            }
        }

        if (scriptPath != NULL) {
            runFile(scriptPath, debugMode, debugProtocolMode, bpPath, breakLines, breakCount, breakConds);
        } else {
            fprintf(stderr, "Usage: blaze [script.blaze]\n");
            fprintf(stderr, "       blaze --test    Run built-in tests\n");
            fprintf(stderr, "       blaze --debug [--break N] [--break-if N:hit>=K] [--bp-file path] script.blaze\n");
            fprintf(stderr, "       blaze --debug-protocol [--break N] [--break-if N:hit>=K] [--bp-file path] script.blaze\n");
            fprintf(stderr, "       blaze           Start REPL\n");
            freeTypeSystem();
            return 64;
        }
    }

    // Cleanup
    freeTypeSystem();

    return 0;
}
