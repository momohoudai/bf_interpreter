// This is purely an exercise for me as an introduction to writing an intepreter
// in hope of one day writing my own programming language.
// 
// A BrainFuck intepreter can obviously be written in a much simpler way than what
// is written here. What is written here is supposed steps taken a modern compiler:
//   Lexing -> Parsing -> AST -> compiling -> runtime
//
// The code is written to as readable as possible by me. I also chose to write in C
// so that I can fully understand what kind of parsers makes sense for a compiler.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define bf_arrcnt(arr) (sizeof(arr)/sizeof(*arr))

static const char* error = 0;

typedef enum {
  BF_TOKEN_TYPE_ADD,            // +
  BF_TOKEN_TYPE_SUB,            // -
  BF_TOKEN_TYPE_SHIFT_LEFT,     // <
  BF_TOKEN_TYPE_SHIFT_RIGHT,    // >
  BF_TOKEN_TYPE_READ,           // ,
  BF_TOKEN_TYPE_WRITE,          // .
  BF_TOKEN_TYPE_BEGIN_LOOP,     // [
  BF_TOKEN_TYPE_END_LOOP,       // ]
} BrainFuck_Token_Type;

typedef struct BrainFuck_Token {
  BrainFuck_Token_Type type;
  int line;
  int at;
} BrainFuck_Token;

typedef struct BrainFuck_Lexer {
  int token_cap;
  int token_count;
  BrainFuck_Token* tokens;
} BrainFuck_Lexer; 


typedef struct BrainFuck_Tokens {
  int cap;
  int count;
  BrainFuck_Token* e;
} BrainFuck_Tokens; 

// The parser's job is to consume the tokens into a set of 'nodes' which
// are instructions for the intepreter to execute. These nodes form the
// Abstract Syntax Tree.
//
// ...Although for BrainFuck, it's just a linked list.
typedef enum BrainFuck_Node_Type {
  BF_NODE_TYPE_LOOP,
  BF_NODE_TYPE_ADD,
  BF_NODE_TYPE_SUB,
  BF_NODE_TYPE_SHIFT_LEFT,
  BF_NODE_TYPE_SHIFT_RIGHT,
  BF_NODE_TYPE_READ,
  BF_NODE_TYPE_WRITE,
} BrainFuck_Node_Type;

typedef struct BrainFuck_AST {
  struct BrainFuck_Node* first;
  struct BrainFuck_Node* last;
} BrainFuck_AST;



typedef struct BrainFuck_AST_Stack {
  int cap;
  int count;
  BrainFuck_AST* e;
} BrainFuck_AST_Stack;

typedef struct BrainFuck_Node_Loop {
  BrainFuck_AST ast;
} BrainFuck_Node_Loop;

typedef struct BrainFuck_Node {
  BrainFuck_Node_Type type;
  struct BrainFuck_Node* next;
  union {
    BrainFuck_Node_Loop loop;
    // others?
  };
} BrainFuck_Node;

typedef struct BrainFuck_Parser {

  BrainFuck_AST root_ast;

  BrainFuck_Node* nodes;
  int node_count;
  int node_cap;
} BrainFuck_Parser;


typedef struct BrainFuck_Allocator {
  int node_count;
  BrainFuck_Node nodes[256];

} BrainFuck_Allocator;

typedef struct BrainFuck_State {
  char data[2000];
  int at;
} BrainFuck_State;

///////////////////////////////////////////////////////////////////////
//
static void
bf_ast_stack_free(BrainFuck_AST_Stack* p) {
  free(p->e);
  p->count = p->cap = 0;
  p->e = 0;
}

static BrainFuck_AST*
bf_ast_stack_push(BrainFuck_AST_Stack* p) {
  const int grow = 10;
  if (p->count >= p->cap) {
    void* mem = realloc(p->e, sizeof(*p->e) * (p->cap += grow));
    if (!mem) return 0;
    p->e = (BrainFuck_AST*)mem;
  }
  BrainFuck_AST* ret = p->e + p->count++;
  ret->first = ret->last = 0;
  return ret ;
}

static void
bf_ast_stack_pop(BrainFuck_AST_Stack* p) {
  if (p->count > 0){
    --p->count;
  }
}

static BrainFuck_AST*
bf_ast_stack_last(BrainFuck_AST_Stack* p) {
  if (p->count > 0) {
    return p->e + p->count - 1;
  }
  return 0;
}

static void
bf_parser_free(BrainFuck_Parser* p) {
  free(p->nodes);
  p->node_count = 0;
  p->node_cap = 0;
  p->nodes = 0;
}


static BrainFuck_Node*
bf_parser_new_node(BrainFuck_Parser* p) {
  assert(p->node_count < p->node_cap);
  BrainFuck_Node* ret = p->nodes + p->node_count++;
  return ret;
}

static void
bf_parser_init_nodes(BrainFuck_Parser* p, int amount) {
  p->nodes = (BrainFuck_Node*)malloc(sizeof(*p->nodes)*amount);
  p->node_cap = amount;
  p->node_count = 0;
}


static void 
bf_ast_push_node(BrainFuck_AST* p, BrainFuck_Node* node) {

  if(!p->first) {
    p->first = p->last = node;
  }
  else {
    p->last->next = node;
    p->last = node;
    p->last->next = 0;
  }
}

static void
bf_interpret_node(BrainFuck_State* state, BrainFuck_Node* node) {
  //printf("at: %d\n", state->at);
  // TODO error handling
  //
  switch(node->type) {
    case BF_NODE_TYPE_ADD: {
      //printf("+\n");
      ++(state->data[state->at]);
    } break;
    case BF_NODE_TYPE_SUB: {
      //printf("-\n");
      --(state->data[state->at]);
    } break;
    case BF_NODE_TYPE_SHIFT_LEFT: {
      //printf("<\n");
      --state->at;
    } break;
    case BF_NODE_TYPE_SHIFT_RIGHT: {
      //printf(">\n");
      ++state->at;
    } break;
    case BF_NODE_TYPE_WRITE: {
      //printf(".\n");
      putchar(state->data[state->at]);
    } break;
    case BF_NODE_TYPE_READ: {
      //printf(",\n");
      state->data[state->at] = getchar();
    } break;
    case BF_NODE_TYPE_LOOP: {
      //printf("[\n");
      // Oh boy
      while(state->data[state->at] != 0) {
        for(BrainFuck_Node* itr = node->loop.ast.first;
            itr != 0;
            itr = itr->next)
        {
          bf_interpret_node(state, itr);
        }
      }
      //printf("]\n");
    } break;

  }
}


static void 
bf_interpret(BrainFuck_Parser* parser) {
  BrainFuck_AST* ast = &parser->root_ast; 
  BrainFuck_State state = {0};

  for(BrainFuck_Node* itr = ast->first; 
      itr != 0; 
      itr = itr->next) 
  {
    bf_interpret_node(&state, itr);
  }
}

static int 
bf_parser_shrink_to_fit_nodes(BrainFuck_Parser* parser) {  
  void* mem = realloc(parser->nodes, sizeof(*parser->nodes) * (parser->node_count));
  if (!mem) return 0;
  parser->nodes = (BrainFuck_Node*)mem;
  parser->node_cap = parser->node_count;
}


static int
bf_parser_parse(BrainFuck_Parser* parser, BrainFuck_Token* tokens, int token_count)  
{
  bf_parser_init_nodes(parser, token_count);
  BrainFuck_AST_Stack stack = {0};
  BrainFuck_AST* cur_ast = bf_ast_stack_push(&stack);


  for (int token_index = 0; 
       token_index < token_count; 
       ++token_index) 
  {
    BrainFuck_Token* token = tokens + token_index;
    switch(token->type) {
      case BF_TOKEN_TYPE_ADD: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_ADD;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_SUB: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_SUB;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_SHIFT_LEFT: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_SHIFT_LEFT;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_SHIFT_RIGHT: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_SHIFT_RIGHT;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_READ: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_READ;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_WRITE: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_WRITE;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_BEGIN_LOOP: {
        BrainFuck_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_LOOP;
        bf_ast_push_node(cur_ast, node);
        cur_ast = bf_ast_stack_push(&stack); 
      } break;
      case BF_TOKEN_TYPE_END_LOOP: {
        BrainFuck_AST* loop_ast = cur_ast;

        bf_ast_stack_pop(&stack);
        cur_ast = bf_ast_stack_last(&stack);

        if (cur_ast == 0) {
          // TODO error
          bf_ast_stack_free(&stack);
          bf_parser_free(parser);
          return 0;
        }
        // Get the last node of the AST which should be a loop node
        assert(cur_ast->last->type == BF_NODE_TYPE_LOOP);
        cur_ast->last->loop.ast = (*loop_ast); 
      } break;
    }
  }

  parser->root_ast = (*cur_ast);
  bf_ast_stack_free(&stack);
  return 1;
}

static void
bf_lexer_free(BrainFuck_Lexer* lexer) {
  free(lexer->tokens);
  lexer->tokens = 0;
  lexer->token_count = 0;
  lexer->token_cap = 0;
}

static int 
bf_lexer_push_token(BrainFuck_Lexer* lexer, BrainFuck_Token_Type type, int at, int line) {
  const int grow = 100;
  if (lexer->token_count >= lexer->token_cap) {
    void* mem = realloc(lexer->tokens, sizeof(*lexer->tokens) * (lexer->token_cap += grow));
    if (!mem) return 0;
    lexer->tokens = (BrainFuck_Token*)mem;
  }
  BrainFuck_Token* token = lexer->tokens + lexer->token_count++;
  token->type = type;
  token->at = at;
  token->line = line;
  return 1;
}

static int 
bf_lexer_shrink_to_fit_tokens(BrainFuck_Lexer* lexer) {  
  void* mem = realloc(lexer->tokens, sizeof(*lexer->tokens) * (lexer->token_count));
  if (!mem) return 0;
  lexer->tokens = (BrainFuck_Token*)mem;
  lexer->token_cap = lexer->token_count;
}

static void
bf_print_token(BrainFuck_Token token) {
#define set_token_case(type) case type: printf(#type); break
  switch(token.type) 
  {
    set_token_case(BF_TOKEN_TYPE_ADD);
    set_token_case(BF_TOKEN_TYPE_SUB);
    set_token_case(BF_TOKEN_TYPE_SHIFT_LEFT);
    set_token_case(BF_TOKEN_TYPE_SHIFT_RIGHT);
    set_token_case(BF_TOKEN_TYPE_READ);
    set_token_case(BF_TOKEN_TYPE_WRITE);
    set_token_case(BF_TOKEN_TYPE_BEGIN_LOOP);
    set_token_case(BF_TOKEN_TYPE_END_LOOP);
  }
#undef set_token_case
}

#if 0
static void 
bf_print_all_tokens(BrainFuck_Tokens* e) {
  for(int i = 0; i < e->count; ++i) {
    printf("[");
    bf_print_token(e->e[i]);
    printf("]\n");
  }
}
#endif


static int 
bf_lexer_lex(BrainFuck_Lexer* lexer, const char* src, int src_size) 
{
  int line = 1;
  for(int i = 0; i < src_size; ++i) {
    switch(src[i]) {
      case '+':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_ADD, i, line);
        break;
      case '-':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_SUB, i, line);
        break;
      case '<':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_SHIFT_LEFT, i, line);
        break;
      case '>':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_SHIFT_RIGHT, i, line);
        break;
      case ',':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_READ, i, line);
        break;
      case '.':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_WRITE, i, line);
        break;
      case '[':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_BEGIN_LOOP, i, line);
        break;
      case ']':
        bf_lexer_push_token(lexer, BF_TOKEN_TYPE_END_LOOP, i, line);
        break;
      case '\n':
        ++line;
        break;
      case ' ':
      case '\t':
      case '\r':
        break;
      default: 
        printf("Unknown token %d at line %d\n", src[i], line); 
        bf_lexer_free(lexer);
        return 0;
    }
  }
  if (!bf_lexer_shrink_to_fit_tokens(lexer)) {
    bf_lexer_free(lexer);
    return 0;
  }
  //bf_print_all_tokens(lexer);
  return 1;
}


static void
bf_execute(const char* src, int src_size) {
  BrainFuck_Lexer lexer = {0};
  BrainFuck_Parser parser = {0};

  if (bf_lexer_lex(&lexer, src, src_size)) 
  {
    if (bf_parser_parse(&parser, lexer.tokens, lexer.token_count))
    {
      bf_interpret(&parser);
      bf_parser_free(&parser);
    }
    else {
      printf("Parse error!\n");
    }
    bf_lexer_free(&lexer);
  }
  else {
    printf("Lex error!\n");
  }
}




static char* 
read_file_as_string(const char* filename, int* out_size) 
{
  char* ret = 0;
  FILE* file = fopen(filename, "r");
  if (file) {
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    ret = (char*)malloc(len);
    fread(ret, len, 1, file);
    (*out_size) = len;
    fclose(file);
  }

  return ret;
}


static void 
run_file(const char* filename) {
  int size = 0;
  char* src = read_file_as_string(filename, &size);
  if (src) {
    bf_execute(src, size);
    free(src);
  }
  else {
    printf("Unable to read file\n");
  }
}


int main(int argc, char** argv) {
  if (argc == 2) {
    run_file(argv[1]);
    if (error != 0) {
      printf("\n[ERROR] %s\n", error);
    }
  }
  else {
    printf("Invalid input\n");
  }

}
