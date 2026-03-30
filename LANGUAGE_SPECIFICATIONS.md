# Blaze Language Specification (BNF)

This document describes the **concrete syntax** of the Blaze language using a **BNF-style grammar**. It is derived from the reference implementation (`src/syntax/parser.c`, `src/syntax/scanner.h`). The grammar is **informative**: the parser uses Pratt precedence and speculative parsing; some rules are therefore **disambiguated at parse time** (notably generic calls `name<T>(args)` vs. binary `<`).

**Notation**

- Non-terminals: `LikeThis`.
- Terminals: `"keyword"`, `punctuation`, or **LEX** rules.
- `ε` means empty.
- `{ X }` means zero or more repetitions; `[ X ]` means optional.
- `|` separates alternatives.

---

## 1. Lexical structure

### 1.1 Whitespace and comments

- Spaces and tabs separate tokens.
- **Newline** (`TOKEN_NEWLINE`) is a token and acts as a **statement terminator** (along with `;` in some positions).
- Line comments: `//` to end of line.

### 1.2 Identifiers and keywords

- **Identifier**: `Identifier` — letters, digits, `_`; cannot start with a digit (scanner rules).
- **Keywords** (reserved):  
  `let`, `const`, `fn`, `return`, `if`, `else`, `while`, `for`, `in` (for-in loops and class variance), `out`, `class`, `interface`, `implements`, `extends`, `this`, `super`, `import`, `module`, `enum`, `match`, `try`, `catch`, `throw`, `finally`, `true`, `false`, `nil`, `print`, `type`, `int`, `float`, `bool`, `string`.

**Note:** `from` in import syntax is **not** a reserved keyword; it is matched as an identifier with spelling `from`.

### 1.3 Literals

```bnf
Integer          ::= /* TOKEN_INT */
Float            ::= /* TOKEN_FLOAT */
String           ::= '"' { stringChar } '"'
stringChar       ::= /* any except '"' and unescaped newline; supports "${" interpolation */
```

String literals may contain `${ expression }` interpolation; the parser rewrites these to concatenation.

### 1.4 Operators and punctuation

Single-character and multi-character tokens include:

`( ) { } [ ] , . : ; + - * / % !`  
`!= = == > >= < <= -> => .. ... && || | ? ?. ??`

---

## 2. Program

```bnf
Program            ::= { Declaration }
Declaration        ::= ImportDecl
                    | VarDecl
                    | ConstDecl
                    | TypeAliasDecl
                    | FunctionDecl
                    | ClassDecl
                    | InterfaceDecl
                    | EnumDecl
                    | Statement
```

Top-level parsing: `Declaration` repeats until `EOF`. **Import** is only recognized as a declaration at `import` (see `importDeclaration`).

---

## 3. Imports

```bnf
ImportDecl         ::= "import" StringLiteral NewlineOrSemi
                    | "import" "{" ImportNameList "}" "from" StringLiteral NewlineOrSemi
ImportNameList     ::= Identifier { "," Identifier }
```

`from` is consumed as an identifier token spelling `from`.

---

## 4. Variables and destructuring

```bnf
VarDecl            ::= "let" SimpleBinding NewlineOrSemi
ConstDecl          ::= "const" SimpleBinding NewlineOrSemi
SimpleBinding      ::= Identifier [ ":" TypeAnnotation ] [ "=" Expression ]
                    | ArrayPattern [ ":" TypeAnnotation ] "=" Expression
                    | ObjectPattern [ ":" TypeAnnotation ] "=" Expression

ArrayPattern       ::= "[" [ ArrayPatternElements ] "]"
ArrayPatternElements ::= ArrayPatternElement { "," ArrayPatternElement }
ArrayPatternElement ::= "..." Identifier
                    | Identifier

ObjectPattern      ::= "{" [ ObjectPatternElements ] "}"
ObjectPatternElements ::= Identifier { "," Identifier }
```

`const` requires an initializer.

---

## 5. Type aliases

```bnf
TypeAliasDecl      ::= "type" Identifier "=" TypeAnnotation NewlineOrSemi
```

---

## 6. Functions

```bnf
FunctionDecl       ::= "fn" [ GenericParams ] Identifier "(" ParamList ")" [ "->" TypeAnnotation ] Block
GenericParams      ::= "<" TypeParam { "," TypeParam } ">"
TypeParam          ::= Identifier   /* generic function: plain name */

ParamList          ::= ε | Parameter { "," Parameter }
Parameter          ::= Identifier ":" TypeAnnotation
```

---

## 7. Classes

```bnf
ClassDecl          ::= "class" Identifier [ ClassGenericParams ]
                       [ "extends" TypeAnnotation ]
                       [ "implements" InterfaceList ]
                       "{" ClassBody "}"

ClassGenericParams ::= "<" ClassTypeParam { "," ClassTypeParam } ">"
ClassTypeParam     ::= [ "out" | "in" ] Identifier [ ":" Identifier ]   /* variance markers + optional interface bound */

InterfaceList      ::= Identifier { "," Identifier }

ClassBody          ::= { ClassMember }
ClassMember        ::= FieldDecl | MethodDecl
FieldDecl          ::= Identifier ":" TypeAnnotation NewlineOrSemi
MethodDecl         ::= "fn" Identifier "(" ParamList ")" [ "->" TypeAnnotation ] Block
```

Methods do not use generic type parameters in the current grammar (class-level generics only).

---

## 8. Interfaces

```bnf
InterfaceDecl      ::= "interface" Identifier "{" InterfaceBody "}"
InterfaceBody      ::= { InterfaceMethod }
InterfaceMethod    ::= "fn" Identifier "(" ParamList ")" [ "->" TypeAnnotation ] NewlineOrSemi
```

---

## 9. Enumerations

```bnf
EnumDecl           ::= "enum" Identifier "{" EnumBody "}"
EnumBody           ::= { EnumVariant [ "," ] Newline }
EnumVariant        ::= Identifier [ "(" TypeList ")" ]
TypeList           ::= TypeAnnotation { "," TypeAnnotation }
```

---

## 10. Type annotations

```bnf
TypeAnnotation     ::= UnionType
UnionType          ::= OptionalType { "|" OptionalType }
OptionalType       ::= PrimaryType [ "?" ]
PrimaryType        ::= "[" TypeAnnotation "]"                    /* array */
                    | "fn" "(" [ TypeAnnotation { "," TypeAnnotation } ] ")" "->" TypeAnnotation
                    | SimpleOrGenericType
SimpleOrGenericType ::= "int" | "float" | "bool" | "string" | "nil"
                    | Identifier [ "<" TypeAnnotation { "," TypeAnnotation } ">" ]
```

`TypeAnnotation` is left-associative for `|` chains; the parser builds a union of all members.

---

## 11. Expressions

Expressions use **Pratt parsing** with the following precedence (high number = binds tighter in practice; see `Precedence` in `parser.c`):

| Level | Operators / forms |
|-------|-------------------|
| Assignment | `=` (invalid assignment target is an error) |
| Logical OR | `\|\|` |
| Logical AND | `&&` |
| Equality | `==` `!=` |
| Comparison | `<` `>` `<=` `>=` (see **generic call** note) |
| Range | `..` |
| Term | `+` `-` |
| Factor | `*` `/` `%` |
| Unary | `!` `-` |
| Call / member / index | `()` `.` `?.` `[]` `<` `>` (generic args) |

```bnf
Expression         ::= AssignmentExpr
AssignmentExpr     ::= LogicalOrExpr [ "=" AssignmentExpr ]   /* only valid lvalues */

LogicalOrExpr      ::= LogicalAndExpr { "||" LogicalAndExpr }
LogicalAndExpr     ::= EqualityExpr { "&&" EqualityExpr }
EqualityExpr       ::= ComparisonExpr { ("==" | "!=") ComparisonExpr }
ComparisonExpr     ::= RangeExpr { ("<" | ">" | "<=" | ">=") RangeExpr }   /* disambiguated vs generic call */
RangeExpr          ::= AdditiveExpr { ".." AdditiveExpr }
AdditiveExpr       ::= MultiplicativeExpr { ("+" | "-") MultiplicativeExpr }
MultiplicativeExpr ::= UnaryExpr { ("*" | "/" | "%") UnaryExpr }
UnaryExpr          ::= ("!" | "-") UnaryExpr | PostfixExpr
PostfixExpr        ::= Primary { Postfix }
Postfix            ::= "(" [ ArgumentList ] ")"              /* call */
                    | "<" TypeArgumentList ">" "(" [ ArgumentList ] ")"  /* explicit generic call */
                    | "." Identifier | "?." Identifier
                    | "[" Expression "]"
ArgumentList       ::= Expression { "," Expression }
TypeArgumentList   ::= TypeAnnotation { "," TypeAnnotation }
Primary            ::= Literal | "this" | SuperCall | "(" Expression ")"
                    | "(" LambdaParams ")" [ "->" TypeAnnotation ] "=>" LambdaBody
                    | "[" ArrayElements "]"
                    | "match" Expression "{" MatchExprCases "}"
                    | Identifier

LambdaParams       ::= [ Parameter { "," Parameter } ]
LambdaBody         ::= Expression | Block
SuperCall          ::= "super" "." Identifier
ArrayElements      ::= ε | ArrayElement { "," ArrayElement }
ArrayElement       ::= "..." Expression | Expression
Literal            ::= Integer | Float | String | "true" | "false" | "nil"
```

**Generic call ambiguity:** If `a <` begins a type-argument list and `(` follows the closing `>`, the parser treats it as `a<Ts>(...)`; otherwise it falls back to binary `<`.

**String interpolation:** `"text ${expr} more"` is not a single literal in the AST; it becomes concatenation of string parts and `toString` applications.

---

## 12. Match expressions

```bnf
MatchExprCases     ::= { MatchExprArm [ "," | NewlineOrSemi ] }
MatchExprArm       ::= Pattern [ "(" DestructureParams ")" ] "=>" Expression
Pattern            ::= "_" | CallExpr              /* call-level for enum variants */
DestructureParams  ::= Identifier { "," Identifier }
```

`MatchExpr` uses `match` as a primary expression (not a statement).

---

## 13. Statements

```bnf
Statement          ::= PrintStmt | IfStmt | WhileStmt | ForStmt | ReturnStmt
                    | MatchStmt | TryStmt | ThrowStmt | Block | ExprStmt

PrintStmt          ::= "print" "(" Expression ")" NewlineOrSemi
IfStmt             ::= "if" Expression Block [ "else" ( "if" IfStmt | Block ) ]
WhileStmt          ::= "while" Expression Block
ForStmt            ::= "for" Identifier [ ":" TypeAnnotation ] "in" Expression Block
ReturnStmt         ::= "return" [ Expression ] NewlineOrSemi
MatchStmt          ::= "match" Expression "{" MatchStmtCases "}"
MatchStmtCases     ::= { MatchStmtArm }
MatchStmtArm       ::= Pattern [ "(" DestructureParams ")" ] ( FatArrowBody | Block )
FatArrowBody       ::= "=>" ( Block | Expression NewlineOrSemi )
                    | Block   /* without => */
TryStmt            ::= "try" Block "catch" "(" Identifier ")" Block [ "finally" Block ]
ThrowStmt          ::= "throw" Expression NewlineOrSemi
Block              ::= "{" { Declaration } "}"
ExprStmt           ::= Expression NewlineOrSemi
```

**Wildcard:** `_` is only a wildcard when it is a single identifier `_` in match pattern position.

---

## 14. Terminators

```bnf
NewlineOrSemi      ::= Newline | ";"
```

After `}` or at `EOF`, newline/semicolon may be omitted where the parser allows.

---

## 15. Implementation notes

- **Maximum arity:** 255 arguments per call (see `finishCall` in `parser.c`).
- **Statement boundary:** Many statements end with newline or `;` via `consumeNewlineOrSemicolon`.
- **This specification** does **not** fully describe the **type system**, **name resolution**, or **bytecode**; see `ARCHITECTURE.md` and the typechecker/compiler sources.

---

## References

- Parser: `src/syntax/parser.c`
- Scanner: `src/syntax/scanner.c`, `src/syntax/scanner.h`
- AST: `src/syntax/ast.h`
