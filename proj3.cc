#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <map>
#include <string>
#include <vector>

#include "execute.h"
#include "lexer.h"
#include "inputbuf.h"

using namespace std;

static LexicalAnalyzer lexer;
static Token lookahead;
static map<string,int> symtab;

//advance lookahead to the next token
static void advance() {
    lookahead = lexer.GetToken();
}

//print a syntax error and quit, not entirely needed but makes it easier for me to read
static void syntax_error(const char* fmt, ...) {
    va_list args; va_start(args, fmt);

    fprintf(stderr, "Syntax error on line %d: ", lookahead.line_no);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");

    va_end(args);

    exit(1);

}

//ensure the current token matches t or error and then advance
static void expect(TokenType t) {
    if (lookahead.token_type != t)
        syntax_error("expected token %d but got %d",
                    (int)t, (int)lookahead.token_type);

    advance();
}

//link two IR lists: a -> b
static InstructionNode* append(InstructionNode* a, InstructionNode* b) {
    if (!a) return b;
    InstructionNode* t = a;
    while (t->next) t = t->next;
    t->next = b;

    return a;
}

//create an assign instruction: lhs = (op1 op op2)
static InstructionNode* makeAssign(int lhs,int op1,ArithmeticOperatorType op,int op2){
    auto n = new InstructionNode();
    n->type = ASSIGN;
    n->assign_inst.lhs_loc = lhs;
    n->assign_inst.op1_loc = op1;
    n->assign_inst.op2_loc = op2;
    n->assign_inst.op = op;
    n->next = nullptr;

    return n;

}

//create an input instruction for var slot v
static InstructionNode* makeInput(int v){
    auto n = new InstructionNode();
    n->type = IN;
    n->input_inst.var_loc = v;
    n->next = nullptr;

    return n;

}

//create an output instruction for var slot v
static InstructionNode* makeOutput(int v){
    auto n = new InstructionNode();

    n->type = OUT;
    n->output_inst.var_loc = v;
    n->next = nullptr;

    return n;

}

//conditional jump: if not (a c b) jump to tgt
static InstructionNode* makeCjmp(ConditionalOperatorType c,int a,int b,InstructionNode* tgt){
    auto n = new InstructionNode();
    n->type = CJMP;
    n->cjmp_inst.condition_op = c;
    n->cjmp_inst.op1_loc      = a;
    n->cjmp_inst.op2_loc      = b;
    n->cjmp_inst.target       = tgt;
    n->next = nullptr;

    return n;
}

//unconditional jump to tgt
static InstructionNode* makeJmp(InstructionNode* tgt){
    auto n = new InstructionNode();

    n->type = JMP;
    n->jmp_inst.target = tgt;
    n->next = nullptr;

    return n;

}

//no operation, used as labels
static InstructionNode* makeNoop(){
    auto n = new InstructionNode();
    n->type = NOOP;
    n->next = nullptr;
    return n;
}

//result of parsing an expression-  code to compute it + slot holding value
struct ExprResult {
    InstructionNode* code;
    int loc;
};

//result of parsing a condition-  code + operator + operand slots
struct CondResult {
    InstructionNode* code;
    ConditionalOperatorType op;
    int left_loc, right_loc;
};

//forward declarations to avoid errors
InstructionNode* parse_Generate_Intermediate_Representation();
InstructionNode* parse_var_section();
InstructionNode* parse_id_list();
InstructionNode* parse_body();
InstructionNode* parse_statement_list();
InstructionNode* parse_statement();
InstructionNode* parse_assign_statement();
InstructionNode* parse_if_statement();
InstructionNode* parse_while_statement();
InstructionNode* parse_for_statement();
InstructionNode* parse_switch_statement();
InstructionNode* parse_output_statement();
InstructionNode* parse_input_statement();
ExprResult       parse_expr();
ExprResult       parse_primary();
CondResult       parse_condition();
vector<int>      parse_inputs();

//starting point: build IR from declarations, ten body, then inputs
InstructionNode* parse_Generate_Intermediate_Representation() {
    advance();
    InstructionNode* decls = parse_var_section();
    InstructionNode* body  = parse_body();
    parse_inputs();
    if (decls) append(decls, body);
    return decls ? decls : body;
}

//parse the var section: id, id
InstructionNode* parse_var_section() {
    parse_id_list();
    expect(SEMICOLON);
    return nullptr;
}

//read comma separated identifiers into symtab
InstructionNode* parse_id_list() {
    do {
        string id = lookahead.lexeme;
        expect(ID);
        symtab[id] = next_available;
        mem[next_available++] = 0;           // initialize slot
    } while (lookahead.token_type == COMMA && (expect(COMMA), true));
    return nullptr;
}

//parse a block of statements in the brackets
InstructionNode* parse_body() {
    expect(LBRACE);
    InstructionNode* stmts = parse_statement_list();
    expect(RBRACE);
    return stmts;
}

//keep parsing statements until } or EOF
InstructionNode* parse_statement_list() {
    InstructionNode* head = parse_statement();
    InstructionNode* tail = head;
    while (tail && tail->next) tail = tail->next;
    while (lookahead.token_type != RBRACE
           && lookahead.token_type != END_OF_FILE) {
        InstructionNode* nxt = parse_statement();
        tail->next = nxt;
        while (tail && tail->next) tail = tail->next;
    }
    return head;
}


InstructionNode* parse_statement() {
    switch (lookahead.token_type) {
      case ID:     return parse_assign_statement();
      case IF:     return parse_if_statement();
      case WHILE:  return parse_while_statement();
      case FOR:    return parse_for_statement();
      case SWITCH: return parse_switch_statement();
      case OUTPUT: return parse_output_statement();
      case INPUT:  return parse_input_statement();
      default:
        syntax_error("unexpected token in stmt");

        return nullptr;

    }
}

//assignment: ID = expr;
InstructionNode* parse_assign_statement() {
    string lhs = lookahead.lexeme; expect(ID);
    int lhs_loc = symtab[lhs];
    expect(EQUAL);

    ExprResult rhs = parse_expr();
    expect(SEMICOLON);

    auto inst = makeAssign(lhs_loc, rhs.loc, OPERATOR_NONE, 0);
    return append(rhs.code, inst);
}

//if without else: IF cond { stmts }
InstructionNode* parse_if_statement() {
    expect(IF);
    CondResult cond = parse_condition();
    expect(LBRACE);
      InstructionNode* thenIR = parse_statement_list();
    expect(RBRACE);

    InstructionNode* skipNoop = makeNoop();
    auto cjmpInst = makeCjmp(cond.op, cond.left_loc, cond.right_loc, skipNoop);
    InstructionNode* head = cond.code ? append(cond.code, cjmpInst) : cjmpInst;

    append(head, thenIR);
    append(thenIR, skipNoop);

    return head;

}

//while loop: WHILE cond { stmts }
InstructionNode* parse_while_statement() {
    expect(WHILE);
    CondResult cond = parse_condition();
    expect(LBRACE);
    InstructionNode* bodyIR = parse_statement_list();
    expect(RBRACE);

    InstructionNode* startLbl = makeNoop();
    InstructionNode* endLbl   = makeNoop();
    InstructionNode* code     = startLbl;

    append(code, cond.code);
    append(code, makeCjmp(cond.op, cond.left_loc, cond.right_loc, endLbl));
    append(code, bodyIR);
    append(code, makeJmp(startLbl->next));
    append(code, endLbl);

    return startLbl->next;
}

//for loop: FOR(init;cond;upd) { stmts }
InstructionNode* parse_for_statement() {
    expect(FOR);
    expect(LPAREN);

    InstructionNode* initIR = parse_assign_statement();
    CondResult cond = parse_condition();
    expect(SEMICOLON);

    InstructionNode* updIR = parse_assign_statement();
    expect(RPAREN);

    expect(LBRACE);
    InstructionNode* bodyIR = parse_statement_list();
    expect(RBRACE);

    InstructionNode* startLbl = makeNoop();
    InstructionNode* endLbl = makeNoop();
    InstructionNode* code = initIR;

    append(code, startLbl);
    append(code, cond.code);
    append(code, makeCjmp(cond.op, cond.left_loc, cond.right_loc, endLbl));
    append(code, bodyIR);
    append(code, updIR);
    append(code, makeJmp(startLbl->next));
    append(code, endLbl);

    return code ? code : startLbl->next;
}

//switch statement lowering using two-phase approach
InstructionNode* parse_switch_statement() {
    expect(SWITCH);
    string var = lookahead.lexeme; expect(ID);
    int varLoc = symtab[var];
    expect(LBRACE);

    //collect each case constant and body
    vector<int> constLocs;
    vector<InstructionNode*> bodies;

    while (lookahead.token_type == CASE) {
        expect(CASE);
        int val = stoi(lookahead.lexeme); expect(NUM);
        expect(COLON);

        int constLoc = next_available;
        mem[next_available++] = val;

        InstructionNode* bodyIR = parse_body();

        constLocs.push_back(constLoc);
        bodies.push_back(bodyIR);

    }

    //default block
    InstructionNode* defaultIR = nullptr;
    if (lookahead.token_type == DEFAULT) {
        expect(DEFAULT);
        expect(COLON);
        defaultIR = parse_body();
    }
    expect(RBRACE);

    //chain of CJMPs to check each case
    InstructionNode* code     = nullptr;
    InstructionNode* endLabel = makeNoop();
    vector<InstructionNode*> caseLabels(constLocs.size());
    //loop through the case labels
    for (size_t i = 0; i < constLocs.size(); ++i) {
        caseLabels[i] = makeNoop();
        code = append(code,
            makeCjmp(CONDITION_NOTEQUAL,
                    varLoc, constLocs[i],
                    caseLabels[i]));
    }

    //if no case matched -> run default or drop to end
    if (defaultIR) {
        code = append(code, defaultIR);
        code = append(code, makeJmp(endLabel));
    } else {
        code = append(code, makeJmp(endLabel));
    }

    //now we leave each case body and jump to end
    for (size_t i = 0; i < bodies.size(); ++i) {
        code = append(code, caseLabels[i]);
        code = append(code, bodies[i]);
        code = append(code, makeJmp(endLabel));
    }

    //final for switch exit
    code = append(code, endLabel);

    return code;
}

//OUTPUT id
InstructionNode* parse_output_statement() {
    expect(OUTPUT);
    string v = lookahead.lexeme; expect(ID);
    expect(SEMICOLON);

    return makeOutput(symtab[v]);
}

//INPUT id
InstructionNode* parse_input_statement() {
    expect(INPUT);
    string v = lookahead.lexeme; expect(ID);
    expect(SEMICOLON);

    return makeInput(symtab[v]);
}

//parse an expression with optional binary operator
ExprResult parse_expr() {
    ExprResult L = parse_primary();
    if (lookahead.token_type==PLUS
     || lookahead.token_type==MINUS
     || lookahead.token_type==MULT
     || lookahead.token_type==DIV)
    {
        TokenType opTk = lookahead.token_type; advance();
        ExprResult R = parse_primary();

        ArithmeticOperatorType aop = OPERATOR_NONE;
        switch (opTk) {
          case PLUS:  aop = OPERATOR_PLUS;  break;
          case MINUS: aop = OPERATOR_MINUS; break;
          case MULT:  aop = OPERATOR_MULT;  break;
          case DIV:   aop = OPERATOR_DIV;   break;
          default: break;
        }

        int tmp = next_available;
        mem[next_available++] = 0;
        auto bin = makeAssign(tmp, L.loc, aop, R.loc);

        //link left code, right code, then return
        InstructionNode* code = append(L.code, R.code);
        code = append(code, bin);
        return {code, tmp};
    }
    return L;
}

//primary value is either a variable slot or a numeric literal
ExprResult parse_primary() {
    if (lookahead.token_type == ID) {
        int loc = symtab[lookahead.lexeme];
        expect(ID);
        return {nullptr, loc};
    }
    if (lookahead.token_type == NUM) {
        int val = stoi(lookahead.lexeme);
        expect(NUM);
        int c = next_available;
        mem[next_available++] = val;     //store literal in mem
        return {nullptr, c};
    }
    syntax_error("expected primary");

    return {nullptr, 0};
}

//condition is primary
CondResult parse_condition() {
    ExprResult L = parse_primary();
    ConditionalOperatorType cop;
    switch (lookahead.token_type) {
      case GREATER:  cop = CONDITION_GREATER; break;
      case LESS:     cop = CONDITION_LESS;    break;
      case NOTEQUAL: cop = CONDITION_NOTEQUAL;break;
      default: syntax_error("expected relational operator");
    }
    advance();

    ExprResult R = parse_primary();
    InstructionNode* code = append(L.code, R.code);

    return {code, cop, L.loc, R.loc};
}

//collect trailing number inputs after the program body
vector<int> parse_inputs() {
    while (lookahead.token_type == NUM) {
        inputs.push_back(stoi(lookahead.lexeme));
        advance();
    }

    return inputs;
}


//YAY IT WORKS