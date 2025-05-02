# IR Compiler (CSE340 Project 3)

A simple compiler/interpreter for a tiny imperative language.  
It parses your code into an intermediate representation (IR) and then “executes” that IR directly.

## Overview

1. **Parsing**  
   - Reads a declaration section (`a, b, c;`)  
   - Parses a statement block (`{ … }`) containing assignments, control flow (`if`, `while`, `for`, `switch`), and I/O  
   - Collects input values for `input` statements  

2. **Lowering to IR**  
   - Builds a linked list of `InstructionNode`s rather than executing the parse tree  
   - Examples:  
     - `x = y + z;` → an `ASSIGN` node  
     - `if x < y { … }` → a `CJMP` (conditional jump) + a `NOOP` label  
     - `switch` → a chain of “if not equal then skip” `CJMP`s + jumps to a shared exit label  

3. **Execution**  
   - A tiny runtime walks the IR list  
   - Maintains a `mem[]` array for variables and constants  
   - Feeds values from a queue for `IN` nodes and prints for `OUT` nodes  
   - Follows `CJMP` and `JMP` pointers to control flow  

## Building & Running

```bash
g++ -std=c++11 -o proj3 proj3.cc execute.cc lexer.cc inputbuf.cc
./proj3 < your_program.txt
