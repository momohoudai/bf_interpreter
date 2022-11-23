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
// If you want to use this for whatever reason, do note that:
// - The lexer and parser does 2 passes; once to count the amount of objects to allocate
// and once more to actually do what it's supposed to do. This is a performance hit
// to avoid the use of some form of realloc.
//
// TODO: 
// - Dynamic data size?
// - Error checking when out of data bounds.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BF_PROFILE 0


#if BF_PROFILE
# include <time.h>
#endif 

static const char* bf_error = 0;

typedef enum {
  BF_TOKEN_TYPE_ADD,            // +
  BF_TOKEN_TYPE_SUB,            // -
  BF_TOKEN_TYPE_SHIFT_LEFT,     // <
  BF_TOKEN_TYPE_SHIFT_RIGHT,    // >
  BF_TOKEN_TYPE_READ,           // ,
  BF_TOKEN_TYPE_WRITE,          // .
  BF_TOKEN_TYPE_BEGIN_LOOP,     // [
  BF_TOKEN_TYPE_END_LOOP,       // ]
} BF_Token_Type;

typedef struct BF_Token {
  BF_Token_Type type;
  int line;
  int at;
} BF_Token;

typedef struct BF_Lexer {
  int token_count;
  BF_Token* tokens;
} BF_Lexer; 


typedef struct BF_Tokens {
  int cap;
  int count;
  BF_Token* e;
} BF_Tokens; 

// The parser's job is to consume the tokens into a set of 'nodes' which
// are instructions for the intepreter to execute. These nodes form the
// Abstract Syntax Tree.
//
// ...Although for BrainFuck, it's just a linked list.
typedef enum BF_Node_Type {
  BF_NODE_TYPE_LOOP,
  BF_NODE_TYPE_ADD,
  BF_NODE_TYPE_SUB,
  BF_NODE_TYPE_SHIFT_LEFT,
  BF_NODE_TYPE_SHIFT_RIGHT,
  BF_NODE_TYPE_READ,
  BF_NODE_TYPE_WRITE,
} BF_Node_Type;

typedef struct BF_AST {
  struct BF_Node* first;
  struct BF_Node* last;
} BF_AST;



typedef struct BF_AST_Stack {
  int cap;
  int count;
  BF_AST* e;
} BF_AST_Stack;

typedef struct BF_Node_Loop {
  BF_AST ast;
} BF_Node_Loop;

typedef struct BF_Node {
  BF_Node_Type type;
  struct BF_Node* next;
  union {
    BF_Node_Loop loop;
    // others?
  };
} BF_Node;

typedef struct BF_Parser {

  BF_AST root_ast;

  BF_Node* nodes;
  int node_count;
} BF_Parser;


typedef struct BF_State {
  char data[2000];
  int at;
} BF_State;

///////////////////////////////////////////////////////////////////////
//
static void
bf_ast_stack_free(BF_AST_Stack* p) {
  free(p->e);
  p->count = p->cap = 0;
  p->e = 0;
}

static BF_AST*
bf_ast_stack_push(BF_AST_Stack* p) {
  BF_AST* ret = p->e + p->count++;
  ret->first = ret->last = 0;

  return ret ;
}

static void
bf_ast_stack_pop(BF_AST_Stack* p) {
  if (p->count > 0){
    --p->count;
  }
}

static BF_AST*
bf_ast_stack_last(BF_AST_Stack* p) {
  if (p->count > 0) {
    return p->e + p->count - 1;
  }
  return 0;
}

static void
bf_parser_free(BF_Parser* p) {
  free(p->nodes);
  p->node_count = 0;
  p->nodes = 0;
}


static BF_Node*
bf_parser_new_node(BF_Parser* p) {
  BF_Node* ret = p->nodes + p->node_count++;
  return ret;
}


static void 
bf_ast_push_node(BF_AST* p, BF_Node* node) {
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
bf_interpret_node(BF_State* state, BF_Node* node) {
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
        for(BF_Node* itr = node->loop.ast.first;
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
bf_interpret(BF_Parser* parser) {
  BF_AST* ast = &parser->root_ast; 
  BF_State state = {0};

  for(BF_Node* itr = ast->first; 
      itr != 0; 
      itr = itr->next) 
  {
    bf_interpret_node(&state, itr);
  }
}
static int
bf_parser_parse(BF_Parser* parser, BF_Token* tokens, int token_count)  
{
  // expected node and AST count
  unsigned total_nodes = 0, total_asts = 0;
  for(int i = 0; i < token_count; ++i) {  
    // Basically, all tokens are a node on its own.
    // Except for [ and ], which is considered 1 node by itself
    if (tokens[i].type != BF_TOKEN_TYPE_END_LOOP) {
      ++total_nodes;
    }
  }
  total_asts = token_count - total_nodes;

#if 0 
  printf("total tokens: %d\n", token_count);
  printf("total nodes: %d\n", total_nodes);
  printf("total asts: %d\n", total_asts);
#endif

  parser->nodes = (BF_Node*)malloc(sizeof(BF_Node)*total_nodes);
  parser->node_count = 0;
  

  BF_AST_Stack stack = {0};
  stack.e = (BF_AST*)malloc(sizeof(BF_AST)*total_asts);
  stack.count = 0;

  BF_AST* cur_ast = bf_ast_stack_push(&stack);


  for (int token_index = 0; 
       token_index < token_count; 
       ++token_index) 
  {
    BF_Token* token = tokens + token_index;
    switch(token->type) {
      case BF_TOKEN_TYPE_ADD: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_ADD;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_SUB: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_SUB;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_SHIFT_LEFT: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_SHIFT_LEFT;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_SHIFT_RIGHT: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_SHIFT_RIGHT;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_READ: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_READ;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_WRITE: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_WRITE;
        bf_ast_push_node(cur_ast, node); 
      } break;
      case BF_TOKEN_TYPE_BEGIN_LOOP: {
        BF_Node* node = bf_parser_new_node(parser);
        node->type = BF_NODE_TYPE_LOOP;
        bf_ast_push_node(cur_ast, node);
        cur_ast = bf_ast_stack_push(&stack); 
      } break;
      case BF_TOKEN_TYPE_END_LOOP: {
        BF_AST* loop_ast = cur_ast;

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

static unsigned
bf_lexer_count_valid_tokens(BF_Lexer* lexer, const char* src, int src_size) {
  const char valid_tokens[] = {'+','-','.',',','<','>','[',']'};
  int ret = 0;
  for(int src_index = 0; src_index < src_size; ++src_index) {
    for(int token_index = 0; token_index < sizeof(valid_tokens); ++token_index){
      if(valid_tokens[token_index] == src[src_index]) {
        ++ret;
        break;
      }
    }
  }
  return ret;
}

static void
bf_lexer_free(BF_Lexer* lexer) {
  free(lexer->tokens);
  lexer->tokens = 0;
  lexer->token_count = 0;
}

static int 
bf_set_token(BF_Token* token, BF_Token_Type type, int at, int line) {
  token->type = type;
  token->at = at;
  token->line = line;
  return 1;
}



static void
bf_print_token(BF_Token token) {
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
bf_print_all_tokens(BF_Tokens* e) {
  for(int i = 0; i < e->count; ++i) {
    printf("[");
    bf_print_token(e->e[i]);
    printf("]\n");
  }
}
#endif


static int 
bf_lexer_lex(BF_Lexer* lexer, const char* src, int src_size) 
{
  unsigned total_tokens = bf_lexer_count_valid_tokens(lexer, src, src_size); 
  BF_Token* tokens = (BF_Token*)malloc(sizeof(*lexer->tokens)*total_tokens);
  if (!tokens) return 0;

#if BF_PROFILE
  clock_t before = clock();
#endif

  int line = 1;
  int token_count = 0;
  for(int i = 0; i < src_size; ++i) {

    switch(src[i]) {
      case '+':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_ADD, i, line);
        break;
      case '-':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_SUB, i, line);
        break;
      case '<':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_SHIFT_LEFT, i, line);
        break;
      case '>':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_SHIFT_RIGHT, i, line);
        break;
      case ',':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_READ, i, line);
        break;
      case '.':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_WRITE, i, line);
        break;
      case '[':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_BEGIN_LOOP, i, line);
        break;
      case ']':
        bf_set_token(tokens + token_count++, BF_TOKEN_TYPE_END_LOOP, i, line);
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

#if BF_PROFILE
  clock_t diff = clock() - before;
  printf("Time taken %f\n", (float)diff/CLOCKS_PER_SEC);
#endif


  lexer->tokens = tokens;
  lexer->token_count = token_count;
  //bf_print_all_tokens(lexer);
  return 1;
}


static void
bf_execute(const char* src, int src_size) {

  BF_Lexer lexer = {0};
  BF_Parser parser = {0};

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
    if (bf_error != 0) {
      printf("\n[ERROR] %s\n", bf_error);
    }
  }
  else {
    printf("Invalid input\n");
  }

}
