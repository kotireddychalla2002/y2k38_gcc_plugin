/*
 * G++ Plugin to detect narrowing casts from 64-bit to 32-bit types.
 * This version manually traverses the AST using the PLUGIN_PRE_GENERICIZE hook.
 * Author: Gemini
 * License: GPLv3
 */

// Define DEBUG_PLUGIN to enable fprintf debugging to stderr.
#ifdef DEBUG_PLUGIN
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

// GCC Plugin Headers (must be first)
#include <gcc-plugin.h>
#include <plugin-version.h>

// GCC Core Headers
#include <tree.h>
#include <cp/cp-tree.h> 
#include <function.h>
#include <tree-iterator.h> 

// GCC Utility Headers
#include <stringpool.h>
#include <tree-pretty-print.h>

// Standard C++ Headers
#include <iostream>

// Required GCC plugin info
int plugin_is_GPL_compatible;
__attribute__((unused))
static struct plugin_info my_plugin_info = {
    .version = "8.0-Final-Complete",
    .help = "Detects 64-to-32 bit narrowing and other lossy numeric conversions.\n"
};

// Data structure to pass information during the traversal.
struct walk_data {
    tree function_return_type;
};

// Forward declaration for our recursive traversal function.
static void traverse_and_check_ast(tree node, walk_data* data);

// Helper to get a string representation of a type.
static const char *get_type_name(tree type) {
    if (!type) return "<null type>";
    if (TYPE_NAME(type) == NULL_TREE) return "<anonymous type>";

    tree name = TYPE_NAME(type);
    if (TREE_CODE(name) == IDENTIFIER_NODE) {
        return IDENTIFIER_POINTER(name);
    }
    if (TREE_CODE(name) == TYPE_DECL) {
        tree decl_name = DECL_NAME(name);
        if (decl_name)
          return IDENTIFIER_POINTER(decl_name);
    }
    return "<unhandled type name>";
}

// Helper to check if a type is an integer or float.
static bool is_numeric_type(tree type) {
    if (!type) return false;
    type = TYPE_MAIN_VARIANT(type);
    return TREE_CODE(type) == INTEGER_TYPE || TREE_CODE(type) == REAL_TYPE;
}

// Helper to get the original type of an expression, looking through casts.
static tree get_original_type(tree expr) {
    if (!expr) return NULL_TREE;
    
    tree current_expr = expr;
    DEBUG_PRINT("    get_original_type on: %s\n", get_tree_code_name(TREE_CODE(current_expr)));

    // For initializers, the actual source is the second operand.
    if (TREE_CODE(current_expr) == INIT_EXPR) {
        current_expr = TREE_OPERAND(current_expr, 1);
        DEBUG_PRINT("      found INIT_EXPR, looking at source operand: %s\n", get_tree_code_name(TREE_CODE(current_expr)));
    }

    // Peel away layers of conversions/no-ops to find the original expression.
    while (current_expr && (TREE_CODE(current_expr) == NOP_EXPR 
                         || TREE_CODE(current_expr) == CONVERT_EXPR
                         || TREE_CODE(current_expr) == VIEW_CONVERT_EXPR
                         || TREE_CODE(current_expr) == FLOAT_EXPR      // FIX: Handle int->float conversion node
                         || TREE_CODE(current_expr) == FIX_TRUNC_EXPR // FIX: Handle float->int conversion node
                         )) {
        current_expr = TREE_OPERAND(current_expr, 0);
        DEBUG_PRINT("      peeled to: %s\n", get_tree_code_name(TREE_CODE(current_expr)));
    }

    tree result_type = current_expr ? TREE_TYPE(current_expr) : TREE_TYPE(expr);
    DEBUG_PRINT("    original type is: %s\n", get_type_name(result_type));
    return result_type;
}

// The core logic to detect narrowing conversion.
static void check_narrowing_conversion(location_t loc, tree to_type, tree from_expr, const char* context) {
    tree from_type = get_original_type(from_expr);
    
    if (!to_type || !from_type || to_type == error_mark_node || from_type == error_mark_node) {
        return;
    }
    
    DEBUG_PRINT("Checking conversion in %s...\n", context);
    DEBUG_PRINT("  To  : %s (precision: %u)\n", get_type_name(to_type), TYPE_PRECISION(to_type));
    DEBUG_PRINT("  From: %s (precision: %u)\n", get_type_name(from_type), TYPE_PRECISION(from_type));

    if (is_numeric_type(to_type) && is_numeric_type(from_type)) {
        tree from_type_main = TYPE_MAIN_VARIANT(from_type);
        tree to_type_main = TYPE_MAIN_VARIANT(to_type);

        tree_code from_code = TREE_CODE(from_type_main);
        tree_code to_code = TREE_CODE(to_type_main);

        unsigned int from_precision = TYPE_PRECISION(from_type);
        unsigned int to_precision = TYPE_PRECISION(to_type);

        // Case 1: Standard narrowing conversion (int64 -> int32, double -> float)
        bool standard_narrowing = (from_precision == 64 && to_precision == 32 && from_code == to_code);
        
        // Case 2: int64 -> float (loss of precision)
        bool int64_to_float = (from_code == INTEGER_TYPE && from_precision == 64 && 
                               to_code == REAL_TYPE && to_precision == 32);

        // Case 3: double -> int32 (loss of precision and range)
        bool double_to_int32 = (from_code == REAL_TYPE && from_precision == 64 &&
                                to_code == INTEGER_TYPE && to_precision == 32);

        if (standard_narrowing || int64_to_float || double_to_int32) {
            DEBUG_PRINT("  >>> POTENTIALLY DANGEROUS CAST DETECTED <<<\n");
            warning_at(loc, 0, "Y2038 potential issue: lossy conversion from %s to %s in %s", 
                       get_type_name(from_type), get_type_name(to_type), context);
        }
    }
}

// Helper function to reliably get the FUNCTION_DECL from a callee expression.
static tree get_fndecl_from_callee_expr(tree callee) {
    if (!callee) return NULL_TREE;

    DEBUG_PRINT("  get_fndecl_from_callee_expr on: %s\n", get_tree_code_name(TREE_CODE(callee)));
    // Peel back layers until we find the FUNCTION_DECL or can't go further.
    while (callee) {
        if (TREE_CODE(callee) == FUNCTION_DECL) {
            return callee;
        }
        if (TREE_CODE(callee) == ADDR_EXPR || 
            TREE_CODE(callee) == NOP_EXPR || 
            TREE_CODE(callee) == CONVERT_EXPR) {
            callee = TREE_OPERAND(callee, 0);
        } else {
            // Can't unwrap further.
            return NULL_TREE;
        }
    }
    return NULL_TREE;
}


// Our manual recursive AST traversal and checking function.
static void traverse_and_check_ast(tree node, walk_data* data) {
    if (node == NULL_TREE) {
        return;
    }

    location_t loc = EXPR_LOCATION(node);
    if (loc == UNKNOWN_LOCATION) {
       loc = input_location;
    }

    DEBUG_PRINT("Traversing node: %s\n", get_tree_code_name(TREE_CODE(node)));

    tree_code code = TREE_CODE(node);

    // Check current node for interesting constructs BEFORE traversing children.
    switch (code) {
        case VAR_DECL: {
            tree initializer = DECL_INITIAL(node);
            if (initializer) {
                tree to_type = TREE_TYPE(node);
                check_narrowing_conversion(DECL_SOURCE_LOCATION(node), to_type, initializer, "variable initialization");
            }
            break;
        }
        case MODIFY_EXPR: { // Handle assignments
            tree lhs = TREE_OPERAND(node, 0);
            tree rhs = TREE_OPERAND(node, 1);
            check_narrowing_conversion(loc, TREE_TYPE(lhs), rhs, "assignment");
            break;
        }
        case CALL_EXPR: { // Handle function calls
            tree fn_decl = get_fndecl_from_callee_expr(CALL_EXPR_FN(node));

            if (!fn_decl) {
                DEBUG_PRINT("  Could not resolve FUNCTION_DECL from callee, skipping.\n");
                break;
            }

            tree fntype = TREE_TYPE(fn_decl);
            DEBUG_PRINT("  Function type is: %s\n", get_type_name(fntype));

            tree arg_types = TYPE_ARG_TYPES(fntype);
            
            tree arg;
            call_expr_arg_iterator iter;
            int arg_num = 0;
            FOR_EACH_CALL_EXPR_ARG(arg, iter, node) {
                DEBUG_PRINT("  Processing arg %d...\n", arg_num++);
                if (!arg_types || TREE_VALUE(arg_types) == void_type_node) {
                    DEBUG_PRINT("    No more parameter types, breaking.\n");
                    break;
                }
                tree param_type = TREE_VALUE(arg_types);
                if (param_type && arg) {
                    check_narrowing_conversion(loc, param_type, arg, "function argument");
                } else {
                    DEBUG_PRINT("    param_type or arg is NULL.\n");
                }
                arg_types = TREE_CHAIN(arg_types);
            }
            break;
        }
        case RETURN_EXPR: { // Handle return statements
            if (TREE_OPERAND(node, 0)) {
                tree retval = TREE_OPERAND(node, 0);
                check_narrowing_conversion(loc, data->function_return_type, retval, "return value");
            }
            break;
        }
        default:
            break;
    }

    // Now, traverse its children based on its type.
    switch (code) {
        // Expressions: Traverse all operands.
        case PLUS_EXPR: case MINUS_EXPR: case MULT_EXPR: case TRUNC_DIV_EXPR:
        case CEIL_DIV_EXPR: case FLOOR_DIV_EXPR: case ROUND_DIV_EXPR:
        case TRUNC_MOD_EXPR: case CEIL_MOD_EXPR: case FLOOR_MOD_EXPR: case ROUND_MOD_EXPR:
        case RDIV_EXPR: case EXACT_DIV_EXPR: case ADDR_EXPR: case FDESC_EXPR:
        case BIT_IOR_EXPR: case BIT_XOR_EXPR: case BIT_AND_EXPR: case BIT_NOT_EXPR:
        case TRUTH_ANDIF_EXPR: case TRUTH_ORIF_EXPR: case TRUTH_AND_EXPR:
        case TRUTH_OR_EXPR: case TRUTH_XOR_EXPR: case TRUTH_NOT_EXPR: case LT_EXPR:
        case LE_EXPR: case GT_EXPR: case GE_EXPR: case EQ_EXPR: case NE_EXPR:
        case UNORDERED_EXPR: case ORDERED_EXPR: case UNLT_EXPR: case UNLE_EXPR:
        case UNGT_EXPR: case UNGE_EXPR: case UNEQ_EXPR: case LTGT_EXPR:
        case INDIRECT_REF: case COMPOUND_EXPR: case MODIFY_EXPR: case INIT_EXPR:
        case TARGET_EXPR: case COND_EXPR: case VEC_COND_EXPR: case VEC_PERM_EXPR:
        case CALL_EXPR: case WITH_CLEANUP_EXPR: case CLEANUP_POINT_EXPR:
        case CONSTRUCTOR: case COMPOUND_LITERAL_EXPR: case SAVE_EXPR:
        case REALIGN_LOAD_EXPR: case CONVERT_EXPR: case NOP_EXPR: case VIEW_CONVERT_EXPR:
        case NON_LVALUE_EXPR: case ABS_EXPR:
        case LSHIFT_EXPR: case RSHIFT_EXPR: case LROTATE_EXPR: case RROTATE_EXPR:
        case FLOAT_EXPR: case FIX_TRUNC_EXPR:
            for (int i = 0; i < TREE_OPERAND_LENGTH(node); ++i) {
                if (TREE_OPERAND(node, i)) {
                    traverse_and_check_ast(TREE_OPERAND(node, i), data);
                }
            }
            break;
            
        // Statements that contain other statements or expressions.
        case BIND_EXPR:
            if (BIND_EXPR_VARS(node)) traverse_and_check_ast(BIND_EXPR_VARS(node), data);
            if (BIND_EXPR_BODY(node)) traverse_and_check_ast(BIND_EXPR_BODY(node), data);
            break;
        case STATEMENT_LIST:
             for (tree_stmt_iterator i = tsi_start(node); !tsi_end_p(i); tsi_next(&i)) {
                traverse_and_check_ast(tsi_stmt(i), data);
            }
            break;
        case EXPR_STMT:
            traverse_and_check_ast(EXPR_STMT_EXPR(node), data);
            break;
        case IF_STMT:
            traverse_and_check_ast(IF_COND(node), data);
            traverse_and_check_ast(THEN_CLAUSE(node), data);
            if (ELSE_CLAUSE(node)) traverse_and_check_ast(ELSE_CLAUSE(node), data);
            break;
         case FOR_STMT:
            traverse_and_check_ast(FOR_INIT_STMT(node), data);
            traverse_and_check_ast(FOR_COND(node), data);
            traverse_and_check_ast(FOR_EXPR(node), data);
            traverse_and_check_ast(FOR_BODY(node), data);
            break; 
        case WHILE_STMT:
            traverse_and_check_ast(WHILE_COND(node), data);
            traverse_and_check_ast(WHILE_BODY(node), data);
            break; 
        case DO_STMT:
            traverse_and_check_ast(DO_BODY(node), data);
            traverse_and_check_ast(DO_COND(node), data);
            break;
        case SWITCH_STMT:
            traverse_and_check_ast(SWITCH_COND(node), data);
            traverse_and_check_ast(SWITCH_BODY(node), data);
            break; 
        case CASE_LABEL_EXPR:
            traverse_and_check_ast(CASE_LOW(node), data);
            if (CASE_HIGH(node)) traverse_and_check_ast(CASE_HIGH(node), data);
            break; 
        case DECL_EXPR:
             traverse_and_check_ast(DECL_EXPR_DECL(node), data);
             break; 
        case VAR_DECL: case PARM_DECL: case FIELD_DECL:
            if (DECL_INITIAL(node)) {
                traverse_and_check_ast(DECL_INITIAL(node), data);
            }
            break;
        default:
            // For any other node type we don't recognize, traverse its operands
            // if it is an expression. This is a safe fallback.
            if (TREE_CODE_CLASS(code) == tcc_expression) {
                 for (int i = 0; i < TREE_OPERAND_LENGTH(node); ++i) {
                    tree op = TREE_OPERAND(node, i);
                    if (op) {
                        traverse_and_check_ast(op, data);
                    }
                }
            }
            break;
    }
}


// Callback for the PLUGIN_PRE_GENERICIZE event.
static void pre_genericize_callback(void *gcc_data, void *user_data) {
    (void)user_data;
    tree fndecl = (tree)gcc_data;
    if (!fndecl) return;

    tree body = DECL_SAVED_TREE(fndecl);
    if (!body) return;

    DEBUG_PRINT("\n--- Processing function: %s ---\n", IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(fndecl)));
    
    walk_data data;
    data.function_return_type = TREE_TYPE(DECL_RESULT(fndecl));
    
    traverse_and_check_ast(body, &data);
}

// Plugin entry point
int plugin_init(struct plugin_name_args *plugin_info, struct plugin_gcc_version *version) {
    if (!plugin_default_version_check(version, &gcc_version)) {
        return 1;
    }
    
    register_callback(plugin_info->base_name, 
                      PLUGIN_PRE_GENERICIZE, 
                      pre_genericize_callback, 
                      NULL);

    return 0;
}


