**overview**

This compiler project parses a tiny imperative language and then “runs” it by translating everything into a simple intermediate form and interpreting that. it’s all handwritten C++ (no fancy parser generators), so you see exactly how each if, while, for, and even switch statement gets broken down into basic jump instructions.

**parsing**


read a list of variable names (e.g. a, b, c;)

consume a block of statements { … } that can include assignments, control flow, I/O, etc.

finally grab a stream of input values to feed input statements

**lowering to IR**
instead of executing the parse tree directly, we build a flat list of InstructionNode objects. for example:

an assignment x = y + z; becomes an ASSIGN node

a comparison and branch (if x < y) turns into a CJMP node plus a NOOP label

switch statements become a short sequence of “if not equal, skip” checks and jumps

**interpreting**
a tiny runtime walks the IR list, keeps a big mem[] array for all variables/constants, pulls values from the input queue for IN, prints on OUT, and follows the CJMP/JMP pointers to steer execution.

this setup makes it really clear how high‑level constructs map down to plain jumps and labels, which is the heart of how compilers implement control flow under the hood.
