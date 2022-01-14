/**
 * @file augyang.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief The augyang core implementation.
 *
 * Copyright (c) 2021 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE

#include <libyang/out.h>
#include <libyang/tree.h>
#include <libyang/tree_edit.h>

#include "augyang.h"
#include "errcode.h"
#include "lens.h"
#include "transform.h"

/**
 * @brief Alignment size of the output text when nesting.
 */
#define SPACE_INDENT 2

/**
 * @brief Check @p RETVAL and call return with @p RETVAL.
 *
 * @param[in] RETVAL Value to check.
 */
#define AY_CHECK_RET(RETVAL) if (RETVAL != 0) {return RETVAL;}

/**
 * @brief Check by @p COND and call return with @p RETVAL.
 *
 * @param[in] COND Boolean expression.
 * @param[in] RETVAL Value to return.
 */
#define AY_CHECK_COND(COND, RETVAL) if (COND) {return RETVAL;}

/**
 * @brief Check by @p COND and call goto.
 *
 * @param[in] COND Boolean expression.
 * @param[in] GOTO Label where to jump.
 */
#define AY_CHECK_GOTO(COND, GOTO) if (COND) {goto GOTO;}

/**
 * @brief Edit size of [sizedarray].
 *
 * @param[in] ARRAY Array in which the size is edited.
 * @param[in] SIZE Value to be written.
 */
#define AY_SET_LY_ARRAY_SIZE(ARRAY, SIZE) \
    *(((LY_ARRAY_COUNT_TYPE *)ARRAY) - 1) = SIZE;

/**
 * @brief Check if the lense cannnot have children.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_NO_CHILD(TAG) (TAG <= L_COUNTER)

/**
 * @brief Check if the lense can have exactly one child.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_ONE_CHILD(TAG) (TAG >= L_SUBTREE)

/**
 * @brief Check if the lense can have more children (not at most one).
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_LENSE_HAS_CHILDREN(TAG) ((TAG == L_CONCAT) || (TAG == L_UNION))

/**
 * @brief Get first child from @p LENSE.
 *
 * @param[in] LENSE Lense to examine. Its type allows to have one or more children.
 * @return Child or NULL.
 */
#define AY_GET_FIRST_LENSE_CHILD(LENSE) \
    (AY_LENSE_HAS_ONE_CHILD(LENSE->tag) ? \
        lens->child : \
        (lens->nchildren ? lens->children[0] : NULL))

/**
 * @brief Get lense from ynode.label.
 *
 * @param[in] YNODE Node ynode which may not have the label set.
 * @return Lense of ay_ynode.label or NULL.
 */
#define AY_LABEL_LENS(YNODE) \
    YNODE->label ? YNODE->label->lens : NULL

/**
 * @brief Get lense from ynode.value.
 *
 * @param[in] YNODE Node ynode which may not have the value set.
 * @return Lense of ay_ynode.value or NULL.
 */
#define AY_VALUE_LENS(YNODE) \
    YNODE->value ? YNODE->value->lens : NULL

/**
 * @brief Get lense from ynode.snode.
 *
 * @param[in] YNODE Node ynode which may not have the snode set.
 * @return Lense of ay_ynode.snode or NULL.
 */
#define AY_SNODE_LENS(YNODE) \
    YNODE->snode ? YNODE->snode->lens : NULL

/**
 * @brief Calculate the index value based on the pointer.
 *
 * @param[in] ARRAY Sized array.
 * @param[in] ITEM_PTR Pointer to item in @p ARRAY.
 */
#define AY_INDEX(ARRAY, ITEM_PTR) \
    (ITEM_PTR - ARRAY)

/**
 * @brief Get address of @p ITEM as if it was in the @p ARRAY1.
 *
 * Remap address @p ITEM (from @p ARRAY2) to the address in @p ARRAY1.
 * The address is calculated based on the index.
 *
 * @param[in] ARRAY1 Sized array to which the address of @p ITEM is converted.
 * @param[in] ARRAY2 Sized array where @p ITEM is located.
 * @param[in] ITEM Pointer to item in @p ARRAY2.
 * @return Address in the @p ARRAY1 or NULL.
 */
#define AY_MAP_ADDRESS(ARRAY1, ARRAY2, ITEM) \
    (ITEM ? &ARRAY1[AY_INDEX(ARRAY2, ITEM)] : NULL)

/**
 * @brief Tag message of the augyang executable.
 */
#define AY_NAME "[augyang]"

/**
 * @brief Maximum identifier size (yang statement identifier).
 */
#define AY_MAX_IDENT_SIZE 64

#define AY_YNODE_LIST_FILES_INDEX    1

/* error codes */

#define AYE_MEMORY 1
#define AYE_LENSE_NOT_FOUND 2
#define AYE_L_REC 3
#define AYE_DEBUG_FAILED 4
#define AYE_IDENT_NOT_FOUND 5
#define AYE_IDENT_LIMIT 6

/**
 * @brief Check if lense tag belongs to ynode.label.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_TAG_IS_LABEL(TAG) \
    ((TAG == L_LABEL) || (TAG == L_KEY) || (TAG == L_SEQ))

/**
 * @brief Check if lense tag belongs to ynode.value.
 *
 * @param[in] TAG Tag of the lense.
 */
#define AY_TAG_IS_VALUE(TAG) \
    ((tag == L_STORE) || (tag == L_VALUE))

/**
 * @brief Prefix of imported yang module which contains extensions for generated yang module.
 */
#define AY_EXT_PREFIX "augex"

/**
 * @brief Extension name for showing the path in the augeas data tree.
 */
#define AY_EXT_PATH "data-path"

/**
 * @brief Extension name for showing the value-yang-path..
 */
#define AY_EXT_VALPATH "value-yang-path"

/**
 * @brief Wrapper for lense node.
 *
 * Interconnection of lense structures is not suitable for comfortable browsing. Therefore, an ay_lnode wrapper has
 * been created that contains a better connection between the nodes.
 * The lnode nodes are stored in the Sized array and the number of nodes should not be modified.
 */
struct ay_lnode {
    struct ay_lnode *parent;    /**< Pointer to the parent node. */
    struct ay_lnode *next;      /**< Pointer to the next sibling node. */
    struct ay_lnode *child;     /**< Pointer to the first child node. */
    uint32_t descendants;       /**< Number of descendants in the subtree where current node is the root. */

    struct lens *lens;          /**< Pointer to lense node. Always set. */
};

/**
 * @brief Type of the ynode.
 */
enum yang_type {
    YN_UNKNOWN = 0,     /**< Unknown or undefined type. */
    YN_LEAF,            /**< Yang statement "leaf". */
    YN_LEAFLIST,        /**< Yang statement "leaf-list". */
    YN_LIST,            /**< Yang statement "list". */
    YN_CONTAINER,       /**< Yang statement "container". */
    YN_KEY,             /**< The node is the key in the yang "list". */
    YN_INDEX,           /**< The node is the key in the yang "list" and is implicitly generated as unsigned integer. */
    YN_VALUE,           /**< Yang statement "leaf". The node was generated to store the augeas node value. */
    YN_ROOT             /**< A special type that is only one in the ynode tree. Indicates the root of the entire tree.
                             It has no printing application only makes writing algorithms easier. */
};

/**
 * @brief Check if the ynode is of type "list" key.
 *
 * @param[in] YNODE Pointer to the examined ynode.
 * @return 1 if it is key otherwise 0.
 */
#define AY_TYPE_LIST_KEY(YNODE) \
    ((YNODE->type == YN_KEY) || (YNODE->type == YN_INDEX))

/**
 * @brief Node for printing the yang node.
 *
 * The ynode node represents the yang data node. It is generally created from a lense with tag L_SUBTREE, but can also
 * be created later, in which case ay_ynode.snode is set to NULL. Nodes are stored in the Sized array, so if a node is
 * added or removed from the array, then the ay_ynode pointers must be reset to the correct address so that the tree
 * structure made sense. Functions ay_ynode_insert_* and ay_ynode_delete_node() are intended for these modifications.
 * These modifications are applied in transformations and they are gradually applied in the ay_ynode_transformations(),
 * which results in a print-ready yang format. The structure of the ynode tree should be adjusted in these
 * transformations. The ynode tree is created by the ynode forest (it has multiple root nodes), which is made up of a
 * lnode tree. In the ynode tree is always one root node with type YN_ROOT.
 *
 * Node ynode is also a bit like an augeas node. The Augeas manages its tree consisting of nodes that consist of
 * a label, value and children. The same items contains ynode. Unfortunately, the ynode tree cannot be the same as the
 * augeas tree, so an yang extension statement containing a path is added to the printed yang nodes to make the mapping
 * between the two trees clear. This path generation is handled in the ay_print_yang_data_path().
 *
 * One ynode can contain multiple labels/values because they can be separated by the union operator ('|', L_UNION).
 * So, for example, if a node has multiple labels, the ay_lnode_next_lv() returns the next one.
 *
 * Note that choice-case node from yang laguage is not considered as a ynode. The ynode nodes are indirectly connected
 * via ay_ynode.choice pointer, which serves as an identifier of that choice-case relationship.
 */
struct ay_ynode {
    struct ay_ynode *parent;    /**< Pointer to the parent node. */
    struct ay_ynode *next;      /**< Pointer to the next sibling node. */
    struct ay_ynode *child;     /**< Pointer to the first child node. */
    uint32_t descendants;       /**< Number of descendants in the subtree where current node is the root. */

    enum yang_type type;        /**< Type of the ynode. */
    struct ay_lnode *snode;     /**< Pointer to the corresponding lnode with lense tag L_SUBTREE.
                                     Can be NULL if the ynode was inserted by some transformation. */
    struct ay_lnode *label;     /**< Pointer to the first 'label' which is lense with tag L_KEY, L_LABEL or L_SEQ.
                                     Can be NULL. */
    struct ay_lnode *value;     /**< Pointer to the first 'value' which is lense with tag L_STORE or L_VALUE.
                                     Can be NULL. */
    struct ay_lnode *choice;    /**< Pointer to the lnode with lense tag L_UNION.
                                     Set if the node is under the influence of the union operator. */
    union {
        uint8_t mandatory;      /**< Yang mandatory-stmt. Value 1 indicates mandatory true, 0 mandatory false. */
        uint8_t min;            /**< Yang min-elements-stmt. Constraint for YN_LIST and YN_LEAFLIST. */
    };
};

struct lprinter_ctx;

/**
 * @brief Callback functions for debug printer.
 */
struct lprinter_ctx_f
{
    void (*main)(struct lprinter_ctx *);        /**< Printer can start by this function. */
    ly_bool (*filter)(struct lprinter_ctx *);   /**< To ignore a node so it doesn't print. */
    void (*transition)(struct lprinter_ctx *);  /**< Transition function to the next node. */
    void (*extension)(struct lprinter_ctx *);   /**< To print extended information for the node. */
};

/**
 * @brief Context for the debug printer.
 */
struct lprinter_ctx
{
    uint32_t space;                 /**< Current indent. */
    void *data;                     /**< General pointer to node. */
    struct lprinter_ctx_f func;     /**< Callbacks to customize the print. */
    struct ly_out *out;             /**< Output to which it is printed. */
};

/**
 * @brief Context for the yang printer.
 */
struct yprinter_ctx
{
    struct augeas *aug;     /**< Augeas context. */
    struct module *mod;     /**< Current Augeas module. */
    struct ay_ynode *tree;  /**< Pointer to the Sized array. */
    struct ly_out *out;     /**< Output to which it is printed. */
    uint32_t space;         /**< Current indent. */
};

/**
 * @brief Get error message base on the code.
 *
 * @param[in] err_code Error code;
 * @return String with message.
 */
const char *
augyang_get_error_message(int err_code)
{
    switch (err_code) {
    case AYE_MEMORY:
        return AY_NAME " ERROR: memory allocation failed.\n";
    case AYE_LENSE_NOT_FOUND:
        return AY_NAME " ERROR: lense was not found.\n";
    case AYE_L_REC:
        return AY_NAME " ERROR: lense with tag \'L_REC\' is not supported.\n";
    case AYE_DEBUG_FAILED:
        return AY_NAME " ERROR: debug test failed.\n";
    case AYE_IDENT_NOT_FOUND:
        return AY_NAME " ERROR: identifier not found. Output YANG is not valid.\n";
    case AYE_IDENT_LIMIT:
        return AY_NAME " ERROR: identifier is too long. Output YANG is not valid.\n";
    default:
        return AY_NAME " INTERNAL ERROR: error message not defined.\n";
    }
}

/**
 * @brief Compare two strings and print them if they differ.
 *
 * @param[in] subject For readability, what is actually compared.
 * @param[in] str1 First string to comparison.
 * @param[in] str2 Second string to comparison.
 * @return 0 on success, otherwise 1.
 */
static int
ay_print_debug_compare(const char *subject, const char *str1, const char *str2)
{
    if (strcmp(str1, str2)) {
        printf(AY_NAME " DEBUG: %s difference\n", subject);
        printf("%s\n", str1);
        printf("----------------------\n");
        printf("%s\n", str2);
        return 1;
    }

    return 0;
}

/**
 * @brief Get main lense which augeas will use for parsing.
 *
 * @param[in] mod Current augeas module.
 * @return Main lense or NULL.
 */
static struct lens *
ay_lense_get_root(struct module *mod)
{
    struct binding *bnd;
    struct value *val;

    /* Print lense. */
    if (mod->autoload) {
        /* root lense is prepared */
        return mod->autoload->lens;
    } else {
        bnd = mod->bindings;
        if (!bnd || !bnd->value) {
            return NULL;
        }

        val = bnd->value;
        if (val->tag != V_LENS) {
            return NULL;
        }

        /* suppose the first lense is the root */
        return val->lens;
    }
}

/**
 * @brief Get name of a file (without filename extension) from path.
 *
 * @param[in] path String containing the path to process.
 * @param[out] name Set to part where filename is is.
 * @param[out] len Length of the name.
 */
static void
ay_get_filename(char *path, char **name, size_t *len)
{
    char *iter;

    if ((*name = strrchr(path, '/'))) {
        *name = *name + 1;
    } else {
        *name = path;
    }

    *len = 0;
    for (iter = *name; iter && (*iter != '.'); iter++) {
        (*len)++;
    }
}

/**
 * @brief Go through all the lenses and set various counters.
 *
 * @param[in] lens Main lense where to start.
 * @param[out] ltree_size Number of lenses.
 * @param[out] yforest_size Number of lenses with L_SUBTREE tag.
 * @param[out] l_rec Flag if some lense has tag L_REC tag.
 */
static void
ay_lense_summary(struct lens *lens, uint32_t *ltree_size, uint32_t *yforest_size, ly_bool *l_rec)
{
    if (lens->tag == L_REC) {
        *l_rec = 1;
        return;
    }

    (*ltree_size)++;
    *yforest_size = lens->tag == L_SUBTREE ?
            *yforest_size + 1 :
            *yforest_size;

    if (AY_LENSE_HAS_NO_CHILD(lens->tag)) {
        return;
    }

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag)) {
        ay_lense_summary(lens->child, ltree_size, yforest_size, l_rec);
    } else {
        for (uint64_t i = 0; i < lens->nchildren; i++) {
            ay_lense_summary(lens->children[i], ltree_size, yforest_size, l_rec);
        }
    }
}

/**
 * @brief Go through all the ynode nodes and increment counter based on the rule.
 *
 * @param[in] forest Forest of ynodes.
 * @param[in] rule Callback function returns 1 to increment counter.
 * @param[out] cnt Counter how many times the @p rule returned 1.
 */
static void
ay_ynode_summary(struct ay_ynode *forest, ly_bool (*rule)(struct ay_ynode *), uint32_t *cnt)
{
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        if (rule(&forest[i])) {
            (*cnt)++;
        }
    }
}

/**
 * @brief Print basic debug information about lense.
 *
 * @param[in] out Output where the data are printed.
 * @param[in] lens Lense to print.
 * @param[in] space Current indent.
 * @param[in] lens_tag String representation of the @p lens tag.
 */
static void
ay_print_lens_node_header(struct ly_out *out, struct lens *lens, int space, const char *lens_tag)
{
    char *filename;
    size_t len;
    uint16_t first_line, first_column;

    ay_get_filename(lens->info->filename->str, &filename, &len);
    first_line = lens->info->first_line;
    first_column = lens->info->first_column;

    ly_print(out, "%*s lens_tag: %s\n", space, "", lens_tag);
    ly_print(out, "%*s location: %.*s, %u, %u\n", space, "", len + 4, filename, first_line, first_column);
}

/**
 * @brief Print debug information about lense and go to the next lense.
 *
 * @param[in,out] ctx Context for printing.
 * @param[in] lens Lense to print.
 */
static void
ay_print_lens_node(struct lprinter_ctx *ctx, struct lens *lens)
{
    char *regex = NULL;
    uint32_t sp;
    struct ly_out *out;

    if (ctx->func.filter && ctx->func.filter(ctx)) {
        ctx->func.transition(ctx);
        return;
    }

    out = ctx->out;
    sp = ctx->space;

    ly_print(out, "%*s {\n", sp, "");
    ctx->space = sp = ctx->space + SPACE_INDENT;

    if (ctx->func.extension) {
        ctx->func.extension(ctx);
    }

    if (lens) {
        switch (lens->tag) {
        case L_DEL:
            ay_print_lens_node_header(out, lens, sp, "L_DEL");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_del_regex: %s\n", sp, "", regex);
            free(regex);
            break;
        case L_STORE:
            ay_print_lens_node_header(out, lens, sp, "L_STORE");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_store_regex: %s\n", sp, "", regex);
            free(regex);
            break;
        case L_VALUE:
            ay_print_lens_node_header(out, lens, sp, "L_VALUE");
            ly_print(out, "%*s lens_value_string: %s\n", sp, "", lens->string->str);
            break;
        case L_KEY:
            ay_print_lens_node_header(out, lens, sp, "L_KEY");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_key_regex: %s\n", sp, "", regex);
            free(regex);
            break;
        case L_LABEL:
            ay_print_lens_node_header(out, lens, sp, "L_LABEL");
            ly_print(out, "%*s lens_label_string: %s\n", sp, "", lens->string->str);
            break;
        case L_SEQ:
            ay_print_lens_node_header(out, lens, sp, "L_SEQ");
            ly_print(out, "%*s lens_seq_string: %s\n", sp, "", lens->string->str);
            break;
        case L_COUNTER:
            ay_print_lens_node_header(out, lens, sp, "L_COUNTER");
            ly_print(out, "%*s lens_counter_string: %s\n", sp, "", lens->string->str);
            break;
        case L_CONCAT:
            ay_print_lens_node_header(out, lens, sp, "L_CONCAT");
            break;
        case L_UNION:
            ay_print_lens_node_header(out, lens, sp, "L_UNION");
            break;
        case L_SUBTREE:
            ay_print_lens_node_header(out, lens, sp, "L_SUBTREE");
            break;
        case L_STAR:
            ay_print_lens_node_header(out, lens, sp, "L_STAR");
            break;
        case L_MAYBE:
            ay_print_lens_node_header(out, lens, sp, "L_MAYBE");
            break;
        case L_REC:
            ay_print_lens_node_header(out, lens, sp, "L_REC");
            ly_print(out, "ay_print_lens_node error: L_REC not supported\n");
            return;
        case L_SQUARE:
            ay_print_lens_node_header(out, lens, sp, "L_SQUARE");
            break;
        default:
            ly_print(out, "ay_print_lens_node error\n");
            return;
        }
        ctx->func.transition(ctx);
    } else {
        ctx->func.transition(ctx);
    }

    sp -= SPACE_INDENT;
    ly_print(out, "%*s }\n", sp, "");
    ctx->space = sp;
}

/**
 * @brief Print debug information about lenses.
 *
 * @param[in] data General pointer to a node (eg lense or ynode).
 * @param[in] func Callback functions for debug printer.
 * @param[in] root_lense Lense where to begin.
 * @param[out] str_out Printed result.
 * @return 0 on success.
 */
static int
ay_print_lens(void *data, struct lprinter_ctx_f *func, struct lens *root_lense, char **str_out)
{
    struct lprinter_ctx ctx = {0};
    struct ly_out *out;
    char *str;

    if (ly_out_new_memory(&str, 0, &out)) {
        return AYE_MEMORY;
    }

    ctx.data = data;
    ctx.func = *func;
    ctx.out = out;
    if (ctx.func.main) {
        ctx.func.main(&ctx);
    } else {
        ay_print_lens_node(&ctx, root_lense);
    }

    ly_out_free(out, NULL, 0);

    *str_out = str;
    return 0;
}

static int ay_print_yang_node(struct yprinter_ctx *ctx, struct ay_ynode *node);

/**
 * @brief Get a name of the lense.
 *
 * @param[in] mod Module where to find lense name.
 * @param[in] lens Lense for which the name is to be found.
 * @return Name of the lense or NULL.
 */
static char *
ay_get_lense_name(struct module *mod, struct lens *lens)
{
    char *ret = NULL;

    if (!lens) {
        return NULL;
    }

    list_for_each(bind_iter, mod->bindings) {
        if (bind_iter->value->lens == lens) {
            ret = bind_iter->ident->str;
            break;
        }
    }

    if ((lens->tag == L_STORE) || (lens->tag == L_KEY)) {
        list_for_each(bind_iter, mod->bindings) {
            if (bind_iter->value->tag != V_REGEXP) {
                continue;
            }
            if (bind_iter->value->regexp == lens->regexp) {
                ret = bind_iter->ident->str;
                break;
            }
        }
    }

    return ret;
}

/**
 * @brief Get Augeas context from module.
 *
 * @param[in] mod Module from which the context is taken.
 * @return Augeas context.
 */
static struct augeas *
ay_get_augeas_ctx1(struct module *mod)
{
    assert(mod);
    return (struct augeas *)mod->bindings->value->info->error->aug;
}

/**
 * @brief Get Augeas context from lense.
 *
 * @param[in] lens Lense from which the context is taken.
 * @return Augeas context.
 */
static struct augeas *
ay_get_augeas_ctx2(struct lens *lens)
{
    assert(lens);
    return (struct augeas *)lens->info->error->aug;
}

/**
 * @brief Get module by the module name.
 *
 * @param[in] aug Augeas context.
 * @param[in] modname Name of the required module.
 * @return Pointer to Augeas module or NULL.
 */
static struct module *
ay_get_module(struct augeas *aug, const char *modname)
{
    struct module *mod = NULL;

    list_for_each(mod_iter, aug->modules) {
        if (!strcmp(mod_iter->name, modname)) {
            mod = mod_iter;
            break;
        }
    }

    return mod;
}

/**
 * @brief Get lense name from specific module.
 *
 * @param[in] modname Name of the module where to look for @p lens.
 * @param[in] lens Lense for which to find the name.
 * @return Lense name or NULL.
 */
static char *
ay_get_lense_name_by_modname(const char *modname, struct lens *lens)
{
    char *ret;
    struct module *mod;

    mod = ay_get_module(ay_get_augeas_ctx2(lens), modname);
    ret = mod ? ay_get_lense_name(mod, lens) : NULL;

    return ret;
}

/**
 * @brief Get lense name from specific module and search using a regular expression.
 *
 * @param[in] aug Augeas context.
 * @param[in] modname Name of the module where to look for @p pattern.
 * @param[in] pattern Regular expression used to find lense.
 * @param[in] ignore_maybe Flag set to 1 to ignore the "{0,1}" substring in @p pattern.
 * Augeas inserts this substring into the @p pattern if it is affected by the '?' (maybe) operator.
 * @return Lense name which has @p pattern in the @p modname module otherwise NULL.
 */
static char *
ay_get_lense_name_by_regex(struct augeas *aug, const char *modname, const char *pattern, ly_bool ignore_maybe)
{
    char *ret = NULL;
    struct module *mod;
    size_t pattern_len, maybe_len = strlen("{0,1}");
    char *found;
    uint64_t cnt_found = 0;
    char *str;

    if (!pattern) {
        return NULL;
    }

    mod = ay_get_module(aug, modname);
    if (!mod) {
        return ret;
    }

    pattern_len = strlen(pattern);
    if (ignore_maybe && (pattern_len > maybe_len) && !strcmp(pattern + (pattern_len - maybe_len), "{0,1}")) {
        pattern_len = pattern_len - maybe_len;
        /* pattern without parentheses */
        pattern++;
        pattern_len -= 2;
    }

    mod = ay_get_module(aug, modname);
    list_for_each(bind_iter, mod->bindings) {
        if (bind_iter->value->tag != V_REGEXP) {
            continue;
        }
        str = bind_iter->value->regexp->pattern->str;
        if (strlen(str) != pattern_len) {
            continue;
        }
        if (!strncmp(str, pattern, pattern_len)) {
            found = bind_iter->ident->str;
            cnt_found++;
        }
    }

    if (cnt_found == 1) {
        ret = found;
    }

    return ret;
}

/**
 * @brief Print module name.
 *
 * @param[in] mod Module whose name is to be printed.
 * @param[in] out Output for printing.
 */
static void
ay_print_yang_module_name(struct module *mod, struct ly_out *out)
{
    char *name, *path;
    size_t namelen;

    path = mod->bindings->value->info->filename->str;
    ay_get_filename(path, &name, &namelen);

    ly_print(out, "%.*s", namelen, name);
}

/**
 * @brief Print opening curly brace and set new indent.
 *
 * @param[in,out] ctx Context for printing.
 */
static void
ay_print_yang_nesting_begin(struct yprinter_ctx *ctx)
{
    ly_print(ctx->out, " {\n");
    ctx->space += SPACE_INDENT;
}

/**
 * @brief Print closing curly brace and set new indent.
 *
 * @param[in,out] ctx Context for printing.
 */
static void
ay_print_yang_nesting_end(struct yprinter_ctx *ctx)
{
    ctx->space -= SPACE_INDENT;
    ly_print(ctx->out, "%*s}\n", ctx->space, "");
}

/**
 * @brief Iterate over to all node's children and call print function.
 *
 * @param[in] ctx Context for printing.
 * @param[node] node Current node which may have children.
 * @return 0 on success.
 */
static int
ay_print_yang_children(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct ay_ynode *iter;

    for (iter = node->child; iter; iter = iter->next) {
        ret = ay_print_yang_node(ctx, iter);
        AY_CHECK_RET(ret);
    }

    return ret;
}

/**
 * @brief Modify the identifier to conform to the constraints of the yang identifier.
 *
 * TODO: complete for all input characters.
 *
 * @param[in] ident Identifier for standardization.
 * @param[out] buffer Buffer in which a valid identifier will be written.
 * @return 0 on success.
 */
static int
ay_get_ident_standardized(char *ident, char *buffer)
{
    int64_t i, j, len, stop;

    stop = (int64_t) strlen(ident);
    for (i = 0, j = 0; i < stop; i++, j++) {
        switch (ident[i]) {
        case '@':
            j--;
            break;
        case '+':
            len = strlen("plus_");
            AY_CHECK_COND(j + len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strcpy(&buffer[j], "plus_");
            j += len - 1;
            break;
        case '-':
            if (j == 0) {
                len = strlen("minus_");
                AY_CHECK_COND(j + len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                strcpy(&buffer[j], "minus_");
                j += len - 1;
            } else {
                AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
                buffer[j] = '-';
            }
            break;
        default:
            AY_CHECK_COND(j >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            buffer[j] = ident[i];
        }
    }

    buffer[j] = '\0';

    return 0;
}

/**
 * @brief Substitution of regular pattern parts.
 *
 * Auxiliary structure for ay_get_regex_standardized()
 */
struct regex_map
{
    const char *aug;    /**< Input string to match. */
    const char *yang;   /**< Substituent satisfying yang language. */
};

/**
 * @brief Modify augeas regular pattern to conform to the constraints of the yang regular pattern.
 *
 * TODO: add reliable conversion.
 *
 * @param[in] rp Regular pattern to check or possibly change.
 * @param[out] regout Regular pattern that conforms to the yang language. The caller must free memory.
 * @return 0 on success.
 */
static int
ay_get_regex_standardized(const struct regexp *rp, char **regout)
{
    char *hit = NULL, *regex;
    uint32_t cnt;

    struct regex_map regmap[] = {
        {"[_.A-Za-z0-9][-_.A-Za-z0-9]*\\\\$?", "[_.A-Za-z0-9][-_.A-Za-z0-9]*"},

        {"#commen((t[^]\\n\\r\\/]|[^]\\n\\r\\/t])[^]\\n\\r\\/]*|)|#comme([^]\\n\\r\\/n][^]\\n\\r\\/]*|)|"
            "#comm([^]\\n\\r\\/e][^]\\n\\r\\/]*|)|#com([^]\\n\\r\\/m][^]\\n\\r\\/]*|)|"
            "#co([^]\\n\\r\\/m][^]\\n\\r\\/]*|)|#c([^]\\n\\r\\/o][^]\\n\\r\\/]*|)|"
            "(#[^]\\n\\r\\/c]|[^]\\n\\r#\\/][^]\\n\\r\\/])[^]\\n\\r\\/]*|#|[^]\\n\\r#\\/]",

            "#commen((t[^\\\\]\\n/]|[^\\\\]\\n/t])[^\\\\]\\n/]*|)|#comme([^\\\\]\\n/n][^\\\\]\\n/]*|)|"
            "#comm([^\\\\]\\n/e][^\\\\]\\n/]*|)|#com([^\\\\]\\n/m][^\\\\]\\n/]*|)|"
            "#co([^\\\\]\\n/m][^\\\\]\\n/]*|)|#c([^\\\\]\\n/o][^\\\\]\\n/]*|)|"
            "(#[^\\\\]\\n/c]|[^\\\\]\\n#/][^\\\\]\\n/])[^\\\\]\\n/]*|#|[^\\\\]\\n#/]"},

        {"[^]\\r\\n]+", "[^\\\\]\\n]+"},
    };
    // TODO: if right side of the regsub rule is bigger -> danger of valgrind error
    struct regex_map regsub[] = {
        {"\\/", "/"}
    };
    const char *regdel[] = {
        "\\r",
    };

    regex = regexp_escape(rp);
    AY_CHECK_COND(!regex, AYE_MEMORY);

    for (uint32_t i = 0; i < sizeof(regmap) / sizeof(struct regex_map); i++) {
        if (!strcmp(regex, regmap[i].aug)) {
            free(regex);
            *regout = strdup(regmap[i].yang);
            AY_CHECK_COND(!(*regout), AYE_MEMORY);
            return 0;
        }
    }

    for (uint32_t i = 0; i < sizeof(regdel) / sizeof(char *); i++) {
        do {
            hit = (char *)strstr(regex, regdel[i]);
            if (hit) {
                /* remove needle from haystack */
                memmove(hit, hit + strlen(regdel[i]), (regex + strlen(regex) + 1) - (hit + strlen(regdel[i])));
            }
        } while (hit);
    }

    for (uint32_t i = 0; i < sizeof(regsub) / sizeof(struct regex_map); i++) {
        do {
            hit = (char *)strstr(regex, regsub[i].aug);
            if (hit) {
                /* remove needle from haystack */
                memmove(hit, hit + strlen(regsub[i].aug), (regex + strlen(regex) + 1) - (hit + strlen(regsub[i].aug)));
                /* make space for regsub[i].yang */
                memmove(hit + strlen(regsub[i].yang), hit, (regex + strlen(regex) + 1) - hit);
                /* copy regsub[i].yang */
                strncpy(hit, regsub[i].yang, strlen(regsub[i].yang));
            }
        } while (hit);
    }

    /* count number of \" character */
    cnt = 0;
    for (uint32_t i = 0; i < strlen(regex); i++) {
        if (regex[i] == '\"') {
            cnt++;
        }
    }
    regex = realloc(regex, strlen(regex) + 1 + sizeof(char) * cnt);
    AY_CHECK_COND(!regex, AYE_MEMORY);
    /* add escape character for \" */
    for (uint32_t i = 0; i < strlen(regex); i++) {
        if (regex[i] == '\"') {
            memmove(&regex[i] + 1, &regex[i], strlen(&regex[i]) + 1);
            regex[i] = '\\';
            i += 1;
        }
    }

    *regout = regex;

    return 0;
}

/**
 * @brief Check if pattern is so simple that can be interpreted as label.
 *
 * @param[in] lens Lense to check.
 * @return 1 if pattern is identifier.
 */
static ly_bool
ay_lense_pattern_is_ident(struct lens *lens)
{
    char *ch;

    if (!lens || ((lens->tag != L_STORE) && (lens->tag != L_KEY))) {
        return 0;
    }

    for (ch = lens->regexp->pattern->str; *ch != '\0'; ch++) {
        if (((*ch < 65) && (*ch != 45)) || ((*ch > 90) && (*ch != 95) && (*ch < 97)) || (*ch > 122)) {
            break;
        }
    }

    return *ch == '\0';
}

/**
 * @brief Evaluate the identifier for the node.
 *
 * @param[in] ctx Current printing context.
 * @param[in] node Node for which the identifier is to be derived.
 * @param[out] buffer Buffer in which the obtained identifier is written.
 * @return 0 on success.
 */
static int
ay_get_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, char *buffer)
{
    int ret = 0;
    char *str = NULL, *tmp;
    struct lens *label, *value, *snode;

    snode = AY_SNODE_LENS(node);
    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);

    if (AY_TYPE_LIST_KEY(node)) {
        if (node->type == YN_INDEX) {
            str = (char *)"_id";
        } else if (ay_lense_pattern_is_ident(label)) {
            str = label->regexp->pattern->str;
        } else if (label->tag == L_KEY) {
            str = ay_get_lense_name(ctx->mod, label);
            str = !str ? (char *)"_id" : str;
        } else if (label->tag == L_SEQ) {
            str = (char *)"_id";
        } else if (value) {
            str = ay_get_lense_name(ctx->mod, value);
            if (!str && (label->tag == L_LABEL)) {
                str = label->string->str;
            } else if (!str) {
                str = (char *)"_id";
            }
        } else {
            return AYE_IDENT_NOT_FOUND;
        }
    } else if (node->type == YN_LIST) {
        if (label->tag == L_LABEL) {
            str = label->string->str;
        } else if (ay_lense_pattern_is_ident(label)) {
            str = label->regexp->pattern->str;
        } else if ((tmp = ay_get_lense_name(ctx->mod, snode))) {
            str = tmp;
        } else if (label->tag == L_KEY) {
            str = ay_get_lense_name(ctx->mod, label);
        } else {
            assert(label->tag == L_SEQ);
            str = label->string->str;
        }
        str = !str ? (char *)"list" : str;
    } else {
        if (node->type == YN_VALUE) {
            str = ay_get_lense_name(ctx->mod, value);
        } else if (!label && value && (value->tag == L_STORE)) {
            str = ay_get_lense_name(ctx->mod, value);
        } else if (ay_lense_pattern_is_ident(label)) {
            str = label->regexp->pattern->str;
        } else if (label && (label->tag == L_KEY)) {
            str = ay_get_lense_name(ctx->mod, label);
        } else if (label && (label->tag == L_SEQ)) {
            str = label->string->str;
        } else if (label && (label->tag == L_LABEL)) {
            str = label->string->str;
        }

        if (!str) {
            str = ay_get_lense_name(ctx->mod, snode);
            str = !str ? (char *)"value" : str;
        }
    }

    AY_CHECK_COND(!str, ret);
    ay_get_ident_standardized(str, buffer);

    return ret;
}

/**
 * @brief Print node identifier according to the yang language.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the identifier will be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    char ident[AY_MAX_IDENT_SIZE];
    char siblings_ident[AY_MAX_IDENT_SIZE];
    struct ay_ynode *iter_start, *iter;
    uint32_t duplicates = 1;

    ret = ay_get_yang_ident(ctx, node, ident);
    AY_CHECK_RET(ret);

    /* Make duplicate identifiers unique. */
    iter_start = node->parent ? node->parent->child : &ctx->tree[0];
    for (iter = iter_start; iter; iter = iter->next) {
        if (iter == node) {
            break;
        }
        ret = ay_get_yang_ident(ctx, iter, siblings_ident);
        AY_CHECK_RET(ret);
        if (!strcmp(ident, siblings_ident)) {
            duplicates++;
        }
    }

    if (duplicates > 1) {
        ly_print(ctx->out, "%s%u", ident, duplicates);
    } else {
        ly_print(ctx->out, "%s", ident);
    }

    return ret;
}

/**
 * @brief Print data-path delimiter.
 *
 * @param[in] ctx Context for printing.
 * @param[in] printed Flag set to 1 if some part of the path was already printed.
 */
static void
ay_print_yang_data_path_delim(struct yprinter_ctx *ctx, ly_bool *printed)
{
    if (*printed) {
        ly_print(ctx->out, "/");
    }
}

/**
 * @brief Print data-path part of the list type.
 *
 * @param[in] ctx Context for printing.
 * @param[in] list Node of type list.
 * @param[in] target Node for which the entire data-path is to be printed.
 */
static void
ay_print_yang_data_path_item_list(struct yprinter_ctx *ctx, struct ay_ynode *list, struct ay_ynode *target)
{
    struct lens *label;
    enum lens_tag labtag;
    char *labstr;

    assert(AY_TYPE_LIST_KEY(list->child));

    label = AY_LABEL_LENS(list);
    if (ay_lense_pattern_is_ident(label)) {
        labtag = L_LABEL;
        labstr = label->regexp->pattern->str;
    } else {
        labtag = label->tag;
        labstr = labtag == L_LABEL ? label->string->str : NULL;
    }

    if ((target == list) && (list->child->type == YN_INDEX)) {
        ly_print(ctx->out, "##");
        ly_print(ctx->out, "%s", labstr);
    } else if (label && (labtag == L_LABEL)) {
        ly_print(ctx->out, "%s", labstr);
    } else {
        ly_print(ctx->out, "$$");
    }
}

/**
 * @brief Print part of data-path for the @p target node.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Iterator which is moved by parents and a part of data-path is printed for each of them.
 * @param[in] target Node for which the data-path is to be printed.
 * @param[out] printed Flag will be set to 1 if some string is printed.
 */
static void
ay_print_yang_data_path_item(struct yprinter_ctx *ctx, struct ay_ynode *node, struct ay_ynode *target,
        ly_bool *printed)
{
    struct lens *label;

    label = AY_LABEL_LENS(node);

    if ((node->type == YN_CONTAINER) || AY_TYPE_LIST_KEY(node)) {
        return;
    }

    ay_print_yang_data_path_delim(ctx, printed);
    if (node->type == YN_LIST) {
        ay_print_yang_data_path_item_list(ctx, node, target);
    } else if (label && (label->tag == L_LABEL)) {
        ly_print(ctx->out, "%s", label->string->str);
    } else {
        ly_print(ctx->out, "$$");
    }
    *printed = 1;
}

/**
 * @brief Recursively print parts of data-path for the @p target node.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Iterator which is moved by parents.
 * @param[in] target Node for which the data-path is to be printed.
 * @param[in,out] printed Flag will be set to 1 if some string is printed.
 */
static void
ay_print_yang_data_path_r(struct yprinter_ctx *ctx, struct ay_ynode *node, struct ay_ynode *target, ly_bool *printed)
{
    if (AY_INDEX(ctx->tree, node) == AY_YNODE_LIST_FILES_INDEX) {
        return;
    }

    ay_print_yang_data_path_r(ctx, node->parent, target, printed);
    ay_print_yang_data_path_item(ctx, node, target, printed);
}

/**
 * @brief Print dat-path for @p node.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the data-path is to be printed.
 */
static void
ay_print_yang_data_path(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    ly_bool printed = 0;

    if (node->parent && (node->type == YN_LIST) && (node->parent->type == YN_ROOT)) {
        /* top-level files list */
        return;
    } else if (node->type == YN_VALUE) {
        return;
    }
    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_PATH " \"", ctx->space, "");
    ay_print_yang_data_path_r(ctx, node->parent, node, &printed);
    ay_print_yang_data_path_item(ctx, node, node, &printed);
    ly_print(ctx->out, "\";\n");
}

/**
 * @brief Iterate over parents and print whole value-yang-path.
 *
 * @param[in] ctx Context for printing.
 * @param[in] stop Node where to stop iterating.
 * @param[in] iter Iterator traveling through parents.
 */
static void
ay_print_yang_value_path_r(struct yprinter_ctx *ctx, struct ay_ynode *stop, struct ay_ynode *iter)
{
    if (iter == stop) {
        return;
    } else {
        ay_print_yang_value_path_r(ctx, stop, iter->parent);
    }

    if (iter->parent != stop) {
        ly_print(ctx->out, "/");
    }
    ay_print_yang_ident(ctx, iter);
}

/**
 * @brief Print value-yang-path.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the value-yang-path is to be printed.
 */
static void
ay_print_yang_value_path(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    uint32_t i;
    struct ay_ynode *start, *iter, *valnode = NULL;
    struct lens *label, *value;

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);

    if (node->parent && (node->type == YN_LIST) && (node->parent->type == YN_ROOT)) {
        /* top-level files list */
        return;
    } else if (!value) {
        return;
    } else if ((node->type == YN_LIST) && label && (label->tag == L_LABEL)) {
        return;
    } else if ((node->type == YN_LEAF) && label && ((label->tag == L_LABEL) || ay_lense_pattern_is_ident(label))) {
        return;
    } else if (AY_TYPE_LIST_KEY(node) || (node->type == YN_VALUE)) {
        return;
    }
    assert((node->type == YN_LEAF) || (node->type == YN_LIST));

    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_VALPATH " \"", ctx->space, "");

    /* set starting node */
    if (node->type == YN_LEAF) {
        start = node->parent;
    } else {
        assert(node->type == YN_LIST);
        start = node;
    }

    /* find value node */
    for (i = 0; i < start->descendants; i++) {
        iter = start + i + 1;
        if ((iter->type == YN_VALUE) && (iter->value->lens == node->value->lens) && !AY_TYPE_LIST_KEY(iter)) {
            valnode = iter;
            break;
        }
    }
    assert(valnode);

    /* print */
    ay_print_yang_value_path_r(ctx, start, valnode);
    ly_print(ctx->out, "\";\n");
}

static void
ay_print_yang_minelements(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->min) {
        ly_print(ctx->out, "%*smin-elements %u;\n", ctx->space, "", node->min);
    }
}

/**
 * @brief For ay_lnode_next_lv(), find next label or value.
 */
#define AY_LV_TYPE_ANY   0

/**
 * @brief For ay_lnode_next_lv(), find next value.
 */
#define AY_LV_TYPE_VALUE 1

/**
 * @brief For ay_lnode_next_lv(), find next label.
 */
#define AY_LV_TYPE_LABEL 2

/**
 * @brief Get next label/value.
 *
 * Next in order may be found if they are separated by the '|' operator (L_UNION).
 *
 * @param[in] lv Node whose lense tag is L_LABEL or L_VALUE. The point from which to look further.
 * @param[in] lv_type Flag specifying the type to search for. See AY_LV_TYPE_* constants.
 * @return Follower or NULL.
 */
static struct ay_lnode *
ay_lnode_next_lv(struct ay_lnode *lv, uint8_t lv_type)
{
    struct ay_lnode *iter, *stop;
    enum lens_tag tag;

    if (!lv) {
        return NULL;
    }

    for (iter = lv->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {}
    assert(iter->lens->tag == L_SUBTREE);

    stop = iter + iter->descendants + 1;
    for (iter = lv + 1; iter < stop; iter++) {
        tag = iter->lens->tag;
        if (tag == L_SUBTREE) {
            iter += iter->descendants;
        } else if (((lv_type == AY_LV_TYPE_LABEL) && AY_TAG_IS_LABEL(tag)) ||
                ((lv_type == AY_LV_TYPE_VALUE) && AY_TAG_IS_VALUE(tag)) ||
                ((lv_type == AY_LV_TYPE_ANY) && (AY_TAG_IS_VALUE(tag) || AY_TAG_IS_VALUE(tag)))) {
            return iter;
        }
    }

    return NULL;
}

/**
 * @brief Print type-stmt string and also pattern-stmt if necessary.
 *
 * @param[in] ctx Context for printing.
 * @param[in] rp Regular expression to printed. It can be NULL.
 * @return 0 on success.
 */
static int
ay_print_yang_type_string(struct yprinter_ctx *ctx, const struct regexp *rp)
{
    int ret = 0;
    char *regex = NULL;

    if (!rp) {
        ly_print(ctx->out, "%*stype string;\n", ctx->space, "");
        return ret;
    }

    ly_print(ctx->out, "%*stype string", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    ret = ay_get_regex_standardized(rp, &regex);
    AY_CHECK_RET(ret);
    ly_print(ctx->out, "%*spattern \"%s\";\n", ctx->space, "", regex);
    free(regex);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Assign a yang type to a specific lense in the module.
 *
 * @param[in] modname Name of the module to which the @p ident belongs.
 * @param[in] ident Name of the lense.
 * @return Yang built-In Type name or NULL.
 */
static const char *
ay_get_yang_type_by_lense_name(const char *modname, const char *ident)
{
    const char *ret = NULL;

    if (!ident) {
        return ret;
    }

    if (!strcmp(modname, "Rx")) {
        if (!strcmp("integer", ident)) {
            ret = "uint64";
        } else if (!strcmp("relinteger", ident) || !strcmp("relinteger_noplus", ident)) {
            ret = "int64_t";
        } else if (!strcmp("reldecimal", ident) || !strcmp("decimal", ident)) {
            ret = "decimal64";
        }
    }

    return ret;
}

/**
 * @brief Print type built-in yang type.
 *
 * @param[in] ctx Context for printing.
 * @param[in] reg Lense containing regular expression to print.
 * @param[in] type_in_union Set flag if current type is printed in union statement.
 * @return 0 if type was printed successfully.
 */
static int
ay_print_yang_type_builtin(struct yprinter_ctx *ctx, struct lens *reg, ly_bool type_in_union)
{
    int ret = 0;
    const char *ident = NULL;
    char *filename = NULL;
    size_t len = 0;
    ly_bool print_union;

    ay_get_filename(reg->regexp->info->filename->str, &filename, &len);

    if (!strncmp(filename, "rx", len)) {
        ident = ay_get_lense_name_by_modname("Rx", reg);
        print_union = 0;
    } else {
        ident = ay_get_lense_name_by_regex(ctx->aug, "Rx", reg->regexp->pattern->str, 1);
        print_union = 1;
    }

    ident = ay_get_yang_type_by_lense_name("Rx", ident);
    if (ident && print_union) {
        if (type_in_union) {
            ly_print(ctx->out, "%*stype %s;\n", ctx->space, "", ident);
            ly_print(ctx->out, "%*stype empty;\n", ctx->space, "");
        } else {
            ly_print(ctx->out, "%*stype union", ctx->space, "");
            ay_print_yang_nesting_begin(ctx);
            ly_print(ctx->out, "%*stype %s;\n", ctx->space, "", ident);
            ly_print(ctx->out, "%*stype empty;\n", ctx->space, "");
            ay_print_yang_nesting_end(ctx);
        }
    } else if (ident) {
        ly_print(ctx->out, "%*stype %s;\n", ctx->space, "", ident);
    } else {
        ret = 1;
    }

    return ret;
}

/**
 * @brief Print yang union-stmt due to more labels/values.
 *
 * @param[in] ctx Context for printing.
 * @param[in] lnode Node of type lnode containing label or value.
 * @param[in] lv_type Flag choosing if @p lnode points to label or value.
 * @return 0 on success.
 */
static int
ay_print_yang_type_union1(struct yprinter_ctx *ctx, struct ay_lnode *lnode, uint8_t lv_type)
{
    int ret = 0;

    ly_print(ctx->out, "%*stype union", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    do {
        if (ay_print_yang_type_builtin(ctx, lnode->lens, 1)) {
            ret = ay_print_yang_type_string(ctx, lnode->lens->regexp);
            AY_CHECK_RET(ret);
        }
        lnode = ay_lnode_next_lv(lnode, lv_type);
    } while (lnode);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang union-stmt due to choice.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node to print. First node that has a particular choice set.
 * @return 0 on success.
 */
static int
ay_print_yang_type_union2(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    uint32_t i;
    struct ay_ynode *iter;
    struct ay_lnode *choice;
    struct lens *value;

    ly_print(ctx->out, "%*stype union", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    choice = node->choice;
    for (i = 0; i < node->parent->descendants; i++) {
        iter = node->parent + i + 1;
        if (iter->choice == choice) {
            value = AY_VALUE_LENS(iter);
            if (ay_print_yang_type_builtin(ctx, value, 1)) {
                ret = ay_print_yang_type_string(ctx, value->regexp);
                AY_CHECK_RET(ret);
            }
        }
    }
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang type-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the type-stmt is to be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_type(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *label, *value;
    struct ay_lnode *lnode;
    uint8_t lv_type;

    if (!node->label && !node->value) {
        return ret;
    }

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if (node->type == YN_VALUE) {
        assert(value && (value->tag == L_STORE));
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else if (label && (label->tag == L_KEY)) {
        lnode = node->label;
        lv_type = AY_LV_TYPE_LABEL;
    } else if (value && (value->tag == L_STORE)) {
        lnode = node->value;
        lv_type = AY_LV_TYPE_VALUE;
    } else {
        ret = ay_print_yang_type_string(ctx, NULL);
        return ret;
    }

    if (node->choice && (node->type == YN_VALUE)) {
        ay_print_yang_type_union2(ctx, node);
    } else if (ay_lnode_next_lv(lnode, lv_type)) {
        /* there is more labels/values: [ key lns1 (store lns2 | store lns3)) ] */
        ret = ay_print_yang_type_union1(ctx, lnode, lv_type);
    } else if (ay_print_yang_type_builtin(ctx, lnode->lens, 0)) {
        /* it is not builtin type, so let's print string type */
        ret = ay_print_yang_type_string(ctx, lnode->lens->regexp);
    }

    return ret;
}

/**
 * @brief Print yang default-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the default-stmt is to be printed.
 * @return 0 on success.
 */
static int
ay_print_yang_default_value(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *value;

    value = AY_VALUE_LENS(node);
    if (!value) {
        return ret;
    }

    if (value->tag != L_VALUE) {
        return ret;
    }

    ly_print(ctx->out, "%*sdefault \"%s\";", ctx->space, "", value->string->str);

    return ret;
}

/**
 * @brief Print yang leaf-list-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_LEAFLIST.
 * @return 0 on success.
 */
static int
ay_print_yang_leaflist(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sleaf-list ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ay_print_yang_minelements(ctx, node);
    ret = ay_print_yang_type(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_data_path(ctx, node);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang mandatory-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for which the mandatory-stmt is to be printed.
 */
static void
ay_print_yang_mandatory(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->mandatory) {
        ly_print(ctx->out, "%*smandatory true;\n", ctx->space, "");
    }
}

/**
 * @brief Print yang leaf-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as leaf-stmt.
 * @return 0 on success.
 */
static int
ay_print_yang_leaf(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ay_print_yang_mandatory(ctx, node);
    ret = ay_print_yang_type(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_default_value(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_data_path(ctx, node);
    ay_print_yang_value_path(ctx, node);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang leaf-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node printed as list key of type leaf.
 * @return 0 on success.
 */
static int
ay_print_yang_leaf_key(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *label;

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");

    ay_print_yang_ident(ctx, node);
    ay_print_yang_nesting_begin(ctx);
    label = AY_LABEL_LENS(node);
    if (label && (label->tag == L_SEQ)) {
        ly_print(ctx->out, "%*stype uint64;\n", ctx->space, "");
    } else if (node->type == YN_INDEX) {
        ly_print(ctx->out, "%*stype uint64;\n", ctx->space, "");
        ly_print(ctx->out, "%*sdescription\n", ctx->space, "");
        ly_print(ctx->out, "%*s\"Implicitly generated list key representing the position in augeas data.\";\n",
                ctx->space + SPACE_INDENT, "");
    } else {
        ret = ay_print_yang_type(ctx, node);
        AY_CHECK_RET(ret);
        ret = ay_print_yang_default_value(ctx, node);
        AY_CHECK_RET(ret);
    }
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang key-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_LIST.
 * @return 0 on success.
 */
static int
ay_print_yang_list_key(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct ay_ynode *iter;

    ly_print(ctx->out, "%*skey \"", ctx->space, "");
    for (iter = node->child; iter; iter = iter->next) {
        if (AY_TYPE_LIST_KEY(iter)) {
            ay_print_yang_ident(ctx, iter);
        }
    }
    ly_print(ctx->out, "\";\n");

    return ret;
}

/**
 * @brief Print yang list of files.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type list which is located on index AY_YNODE_LIST_FILES_INDEX.
 * @return 0 on success.
 */
static int
ay_print_yang_list_files(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    assert(AY_INDEX(ctx->tree, node) == AY_YNODE_LIST_FILES_INDEX);

    ly_print(ctx->out, "%*slist ", ctx->space, "");
    ay_print_yang_module_name(ctx->mod, ctx->out);
    ay_print_yang_nesting_begin(ctx);

    ly_print(ctx->out, "%*skey \"config-file\";\n", ctx->space, "");
    ly_print(ctx->out, "%*smin-elements 1;\n", ctx->space, "");
    ly_print(ctx->out, "%*sleaf config-file", ctx->space, "");
    ay_print_yang_nesting_begin(ctx);
    ly_print(ctx->out, "%*stype string;\n", ctx->space, "");
    ay_print_yang_nesting_end(ctx);

    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang list-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_LIST.
 * @return 0 on success.
 */
static int
ay_print_yang_list(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    if (AY_INDEX(ctx->tree, node) == AY_YNODE_LIST_FILES_INDEX) {
        ay_print_yang_list_files(ctx, node);
        return ret;
    }

    ly_print(ctx->out, "%*slist ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ret = ay_print_yang_list_key(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_minelements(ctx, node);
    ay_print_yang_data_path(ctx, node);
    ay_print_yang_value_path(ctx, node);
    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Print yang container-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node of type YN_CONTAINER.
 * @return 0 on success.
 */
static int
ay_print_yang_container(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*scontainer ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);
    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

/**
 * @brief Actions for node of type YN_UNKNOWN.
 *
 * TODO remove?
 */
static int
ay_print_yang_unknown(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    (void)ctx;
    (void)node;
    return 0;
}

/**
 * @brief Print node based on type.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node to print.
 * @return 0 on success.
 */
static int
ay_print_yang_node_(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret;

    switch (node->type) {
    case YN_UNKNOWN:
        ret = ay_print_yang_unknown(ctx, node);
        break;
    case YN_LEAF:
        ret = ay_print_yang_leaf(ctx, node);
        break;
    case YN_LEAFLIST:
        ret = ay_print_yang_leaflist(ctx, node);
        break;
    case YN_LIST:
        ret = ay_print_yang_list(ctx, node);
        break;
    case YN_CONTAINER:
        ret = ay_print_yang_container(ctx, node);
        break;
    case YN_KEY:
    case YN_INDEX:
        ret = ay_print_yang_leaf_key(ctx, node);
        break;
    case YN_VALUE:
        ret = ay_print_yang_leaf(ctx, node);
        break;
    case YN_ROOT:
        ret = ay_print_yang_children(ctx, node);
        break;
    }

    return ret;
}

/**
 * @brief Print yang case-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for case identifier evaluation.
 * @return 0 on success.
 */
static int
ay_print_yang_case(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    ly_print(ctx->out, "%*scase ", ctx->space, "");
    ay_print_yang_ident(ctx, node);

    return 0;
}

/**
 * @brief Print yang choice-stmt.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Node for case identifier evaluation.
 * @return 0 on success.
 */
static int
ay_print_yang_choice(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->parent) {
        ly_print(ctx->out, "%*schoice ch_", ctx->space, "");
        ay_print_yang_ident(ctx, node->parent);
    } else {
        ly_print(ctx->out, "%*schoice %s", ctx->space, "", ctx->mod->name);
    }

    return 0;
}

/**
 * @brief Recursively print subtree.
 *
 * @param[in] ctx Context for printing.
 * @param[in] node Subtree to print.
 * @return 0 on success.
 */
static int
ay_print_yang_node(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    ly_bool first = 0, alone, last, next_has_same_choice;
    struct ay_ynode *iter, *iter_start;
    struct ay_lnode *choice;

    if (!node->choice) {
        return ay_print_yang_node_(ctx, node);
    }

    choice = node->choice;
    iter_start = node->parent ? node->parent->child : &ctx->tree[0];
    for (iter = iter_start; iter; iter = iter->next) {
        if (iter->choice == choice) {
            first = iter == node ? 1 : 0;
            break;
        }
    }

    next_has_same_choice = node->next && (node->next->choice == choice);
    alone = first && !next_has_same_choice;
    last = !first && !next_has_same_choice;

    if ((node->type == YN_VALUE) && (first || alone)) {
        return ay_print_yang_node_(ctx, node);
    } else if (node->type == YN_VALUE) {
        return ret;
    }

    if (alone) {
        /* choice with one 'case' is not printed */
        ay_print_yang_node_(ctx, node);
    } else if (first && !last) {
        /* print choice */
        ay_print_yang_choice(ctx, node);
        /* start of choice nesting */
        ay_print_yang_nesting_begin(ctx);

        ay_print_yang_case(ctx, node);
        ay_print_yang_nesting_begin(ctx);
        ay_print_yang_node_(ctx, node);
        ay_print_yang_nesting_end(ctx);
    } else if (!last) {
        /* print case */
        ay_print_yang_case(ctx, node);
        ay_print_yang_nesting_begin(ctx);
        ay_print_yang_node_(ctx, node);
        ay_print_yang_nesting_end(ctx);
    } else {
        /* print last case */
        ay_print_yang_case(ctx, node);
        ay_print_yang_nesting_begin(ctx);
        ay_print_yang_node_(ctx, node);
        ay_print_yang_nesting_end(ctx);
        /* end of choice nesting */
        ay_print_yang_nesting_end(ctx);
    }

    return ret;
}

/**
 * @brief Print ynode tree in yang format.
 *
 * @param[in] mod Module in which the tree is located.
 * @param[in] tree Ynode tree to print.
 * @param[out] str_out Printed tree in yang format. Call free() after use.
 * @return 0 on success.
 */
static int
ay_print_yang(struct module *mod, struct ay_ynode *tree, char **str_out)
{
    int ret;
    struct yprinter_ctx ctx;
    struct ly_out *out;
    char *str;

    if (ly_out_new_memory(&str, 0, &out)) {
        return AYE_MEMORY;
    }

    ctx.aug = ay_get_augeas_ctx1(mod);
    ctx.mod = mod;
    ctx.tree = tree;
    ctx.out = out;
    ctx.space = SPACE_INDENT;

    ly_print(out, "module ");
    ay_print_yang_module_name(ctx.mod, ctx.out);
    ly_print(out, " {\n");
    ly_print(out, "  namespace \"aug:");
    ay_print_yang_module_name(ctx.mod, ctx.out);
    ly_print(out, "\";\n");
    ly_print(out, "  prefix aug;\n\n");
    ly_print(out, "  import augeas-extension {\n");
    ly_print(out, "    prefix " AY_EXT_PREFIX ";\n");
    ly_print(out, "  }\n\n");
    ly_print(out, "  " AY_EXT_PREFIX ":augeas-mod-name \"%s\";\n", mod->name);
    ly_print(out, "\n");

    ret = ay_print_yang_node(&ctx, tree);

    ly_print(out, "}");
    ly_out_free(out, NULL, 0);

    *str_out = str;
    return ret;
}

/**
 * @brief Does nothing.
 *
 * @param[in] ctx Context for printing.
 */
static void
ay_print_void(struct lprinter_ctx *ctx)
{
    (void)(ctx);
    return;
}

/**
 * @brief Check if lense node is not ynode.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of lense.
 * @return 1 if lense should be filtered because it is not ynode, otherwise 0.
 */
static ly_bool
ay_print_lens_filter_ynode(struct lprinter_ctx *ctx)
{
    struct lens *lens;

    lens = ctx->data;
    return !(lens->tag == L_SUBTREE);
}

/**
 * @brief Transition from one lense node to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of lense.
 */
static void
ay_print_lens_transition(struct lprinter_ctx *ctx)
{
    struct lens *lens = ctx->data;

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag)) {
        ctx->data = lens->child;
        ay_print_lens_node(ctx, ctx->data);
    } else if (AY_LENSE_HAS_CHILDREN(lens->tag)) {
        for (uint64_t i = 0; i < lens->nchildren; i++) {
            ctx->data = lens->children[i];
            ay_print_lens_node(ctx, ctx->data);
        }
    }
}

/**
 * @brief Transition from one lnode to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of lnode.
 */
static void
ay_print_lnode_transition(struct lprinter_ctx *ctx)
{
    struct ay_lnode *node, *iter;

    node = ctx->data;
    for (iter = node->child; iter; iter = iter->next) {
        assert(iter->parent == node);
        ctx->data = iter;
        ay_print_lens_node(ctx, iter->lens);
    }
}

/**
 * @brief Print labels and values of @p node.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 * @param[in] node Node whose labels and values is to be printed.
 */
static void
ay_print_ynode_label_value(struct lprinter_ctx *ctx, struct ay_ynode *node)
{
    struct ay_lnode *iter;

    void (*transition)(struct lprinter_ctx *);
    void (*extension)(struct lprinter_ctx *);

    if (!node || (!node->label && !node->value)) {
        return;
    }

    transition = ctx->func.transition;
    extension = ctx->func.extension;

    ctx->func.transition = ay_print_void;
    ctx->func.extension = NULL;

    for (iter = node->label; iter; iter = ay_lnode_next_lv(iter, AY_LV_TYPE_LABEL)) {
        ay_print_lens_node(ctx, iter->lens);
    }
    for (iter = node->value; iter; iter = ay_lnode_next_lv(iter, AY_LV_TYPE_VALUE)) {
        ay_print_lens_node(ctx, iter->lens);
    }

    ctx->func.transition = transition;
    ctx->func.extension = extension;
}

/**
 * @brief Transition from one ynode node to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_transition(struct lprinter_ctx *ctx)
{
    struct ay_ynode *node, *iter;

    node = ctx->data;

    for (iter = node->child; iter; iter = iter->next) {
        assert(iter->parent == node);
        ctx->data = iter;
        if (iter->snode) {
            ay_print_lens_node(ctx, iter->snode->lens);
        } else {
            ay_print_lens_node(ctx, NULL);
        }
    }
}

/**
 * @brief Print node's labels and values then make transition from one ynode to another.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_transition_lv(struct lprinter_ctx *ctx)
{
    ay_print_ynode_label_value(ctx, ctx->data);
    ay_print_ynode_transition(ctx);
}

/**
 * @brief Starting function for printing ynode forest.
 *
 * @param[in,out] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_main(struct lprinter_ctx *ctx)
{
    struct ay_ynode *forest;

    forest = ctx->data;
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        ctx->data = &forest[i];
        if (forest[i].snode) {
            ay_print_lens_node(ctx, forest[i].snode->lens);
        } else {
            ay_print_lens_node(ctx, NULL);
        }
        i += forest[i].descendants;
    }
}

/**
 * @brief Print additional information about ynode.
 *
 * @param[in] ctx Context for printing. The lprinter_ctx.data is type of ynode.
 */
static void
ay_print_ynode_extension(struct lprinter_ctx *ctx)
{
    struct ay_ynode *node;

    node = ctx->data;

    switch (node->type) {
    case YN_UNKNOWN:
        ly_print(ctx->out, "%*s ynode_tag: YN_UNKNOWN\n", ctx->space, "");
        break;
    case YN_LEAF:
        ly_print(ctx->out, "%*s ynode_tag: YN_LEAF\n", ctx->space, "");
        break;
    case YN_LEAFLIST:
        ly_print(ctx->out, "%*s ynode_tag: YN_LEAFLIST\n", ctx->space, "");
        break;
    case YN_LIST:
        ly_print(ctx->out, "%*s ynode_tag: YN_LIST\n", ctx->space, "");
        break;
    case YN_CONTAINER:
        ly_print(ctx->out, "%*s ynode_tag: YN_CONTAINER\n", ctx->space, "");
        break;
    case YN_KEY:
        ly_print(ctx->out, "%*s ynode_tag: YN_KEY\n", ctx->space, "");
        break;
    case YN_INDEX:
        ly_print(ctx->out, "%*s ynode_tag: YN_INDEX\n", ctx->space, "");
        break;
    case YN_VALUE:
        ly_print(ctx->out, "%*s ynode_tag: YN_VALUE\n", ctx->space, "");
        break;
    case YN_ROOT:
        ly_print(ctx->out, "%*s ynode_tag: YN_ROOT\n", ctx->space, "");
        break;
    }

    if (node->choice) {
        ly_print(ctx->out, "%*s choice_id: %p\n", ctx->space, "", node->choice);
    }

    if (node->mandatory) {
        if ((node->type == YN_LIST) || (node->type == YN_LEAFLIST)) {
            ly_print(ctx->out, "%*s min-elements: %u\n", ctx->space, "", node->min);
        } else {
            ly_print(ctx->out, "%*s mandatory: true\n", ctx->space, "");
        }
    }
}

/**
 * @brief Test if lnode tree matches lense tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] mod Module in which the trees are located.
 * @param[in] tree Tree of lnodes to check by print functions.
 * @return 0 on success.
 */
static int
ay_lnode_debug_tree(uint64_t vercode, struct module *mod, struct ay_lnode *tree)
{
    int ret = 0;
    char *str1, *str2;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return ret;
    }

    ret = augyang_print_input_lenses(mod, &str1);
    AY_CHECK_RET(ret);

    print_func.transition = ay_print_lnode_transition;
    ret = ay_print_lens(tree, &print_func, tree->lens, &str2);
    AY_CHECK_RET(ret);

    ret = ay_print_debug_compare("lnode tree", str1, str2);
    if (!ret && (vercode & AYV_LTREE)) {
        printf("%s\n", str2);
    }

    free(str1);
    free(str2);

    return ret;
}

/**
 * @brief Test if ynode forest matches lense tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] mod Module in which the trees are located.
 * @param[in] yforest Forest of ynodes to check by print functions.
 * @return 0 on success.
 */
static int
ay_ynode_debug_forest(uint64_t vercode, struct module *mod, struct ay_ynode *yforest)
{
    int ret = 0;
    char *str1, *str2;
    struct lens *lens;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return ret;
    }

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);
    print_func.transition = ay_print_lens_transition;
    print_func.filter = ay_print_lens_filter_ynode;
    ret = ay_print_lens(lens, &print_func, lens, &str1);
    AY_CHECK_RET(ret);

    print_func.main = ay_print_ynode_main;
    print_func.transition = ay_print_ynode_transition;
    print_func.filter = NULL;
    ret = ay_print_lens(yforest, &print_func, yforest->snode->lens, &str2);
    AY_CHECK_RET(ret);

    ret = ay_print_debug_compare("ynode forest", str1, str2);

    free(str1);
    free(str2);

    return ret;
}

/**
 * @brief Print ynode tree.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] vermask Bitmask for vercode to decide if result should be printed to stdout.
 * @param[in] tree Tree of ynodes to check by print functions.
 * @return 0 on success.
 */
static int
ay_ynode_debug_tree(uint64_t vercode, uint64_t vermask, struct ay_ynode *tree)
{
    int ret = 0;
    char *str1;
    struct lprinter_ctx_f print_func = {0};

    assert(tree->type == YN_ROOT);

    if (!vercode) {
        return 0;
    }

    print_func.transition = ay_print_ynode_transition_lv;
    print_func.extension = ay_print_ynode_extension;
    ret = ay_print_lens(tree, &print_func, NULL, &str1);
    AY_CHECK_RET(ret);

    if (vercode & vermask) {
        printf("%s\n", str1);
    }
    free(str1);

    return ret;
}

int
augyang_print_input_lenses(struct module *mod, char **str)
{
    int ret = 0;
    struct lens *lens;
    struct lprinter_ctx_f print_func = {0};

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);
    print_func.transition = ay_print_lens_transition;
    ret = ay_print_lens(lens, &print_func, lens, str);

    return ret;
}

/**
 * @brief Create lnode tree from lense tree.
 *
 * @param[in] root Root lense of the tree of lenses.
 * @param[in] lens Iterator over lense nodes.
 * @param[out] node Node will contain @p lens.
 */
static void
ay_lnode_create_tree(struct ay_lnode *root, struct lens *lens, struct ay_lnode *node)
{
    struct ay_lnode *child, *prev_child;

    LY_ARRAY_INCREMENT(root);
    node->lens = lens;

    if (AY_LENSE_HAS_NO_CHILD(lens->tag)) {
        /* values are set by the parent */
        return;
    }

    child = node + 1;
    node->child = child;
    child->parent = node;
    ay_lnode_create_tree(root, AY_GET_FIRST_LENSE_CHILD(lens), child);
    node->descendants = 1 + child->descendants;

    if (AY_LENSE_HAS_ONE_CHILD(lens->tag)) {
        return;
    }

    prev_child = child;
    for (uint64_t i = 1; i < lens->nchildren; i++) {
        child = &root[LY_ARRAY_COUNT(root)];
        child->parent = node;
        prev_child->next = child;
        ay_lnode_create_tree(root, node->lens->children[i], child);
        node->descendants += 1 + child->descendants;
        prev_child = child;
    }
}

/**
 * @brief Create basic ynode forest from lnode tree.
 *
 * Only ay_ynode.snode and ay_ynode.descendants are set.
 *
 * @param[out] ynode Tree of ynodes as destination.
 * @param[in] lnode Tree of lnodes as source.
 */
static void
ay_ynode_create_forest_(struct ay_ynode *ynode, struct ay_lnode *lnode)
{
    for (uint32_t i = 0, j = 0; i < lnode->descendants; i++) {
        if (lnode[i].lens->tag == L_SUBTREE) {
            LY_ARRAY_INCREMENT(ynode);
            ynode[j].snode = &lnode[i];
            ynode[j].descendants = 0;
            for (uint32_t k = 0; k < lnode[i].descendants; k++) {
                if (lnode[i + 1 + k].lens->tag == L_SUBTREE) {
                    ynode[j].descendants++;
                }
            }
            j++;
        }
    }
}

/**
 * @brief Connect ynode top-nodes (set ay_ynode.next).
 *
 * @param[in,out] forest Forest of ynodes.
 */
static void
ay_ynode_forest_connect_topnodes(struct ay_ynode *forest)
{
    struct ay_ynode *last;

    if (!LY_ARRAY_COUNT(forest)) {
        return;
    }

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        if (!forest[i].parent) {
            last = &forest[i];
            forest[i].next = last->descendants ? last + last->descendants + 1 : last + 1;
        }
    }
    last->next = NULL;
}

/**
 * @brief Set label and value to all ynodes in the forest.
 *
 * @param[in,out] forest Forest of ynodes.
 */
static void
ay_ynode_add_label_value(struct ay_ynode *forest)
{
    struct ay_ynode *ynode;
    struct ay_lnode *lnode;
    enum lens_tag tag;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        ynode = &forest[i];
        for (uint32_t j = 0; j < ynode->snode->descendants; j++) {
            lnode = &ynode->snode[j + 1];
            tag = lnode->lens->tag;
            if (tag == L_SUBTREE) {
                /* skip - this subtree will be processed later */
                j += lnode->descendants;
            } else if (!ynode->label && AY_TAG_IS_LABEL(tag)) {
                ynode->label = lnode;
            } else if (!ynode->value && AY_TAG_IS_VALUE(value)) {
                ynode->value = lnode;
            }
        }
    }
}

/**
 * @brief Set choice to all ynodes in the forest.
 *
 * @param[in,out] forest Forest of ynodes.
 */
static void
ay_ynode_add_choice(struct ay_ynode *forest)
{
    struct ay_lnode *iter;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        for (iter = forest[i].snode->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
            if (iter->lens->tag == L_UNION) {
                forest[i].choice = iter;
                break;
            }
        }
    }
}

/**
 * @brief Set correct parent, next and child pointers to all ynodes.
 *
 * @param[in,out] tree Tree of ynodes which have bad pointers. It can also take the form of a forest.
 */
static void
ay_ynode_tree_correction(struct ay_ynode *tree)
{
    struct ay_ynode *parent, *iter, *next;
    uint32_t sum;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        parent = &tree[i];
        iter = parent->descendants ? parent + 1 : NULL;
        parent->child = iter;
        sum = 0;
        while (iter) {
            iter->parent = parent;
            iter->child = iter->descendants ? iter + 1 : NULL;
            sum += iter->descendants + 1;
            next = sum != parent->descendants ? iter + iter->descendants + 1 : NULL;
            iter->next = next;
            iter = next;
        }
    }
}

/**
 * @brief Create forest of ynodes from lnode tree.
 *
 * @param[in] ltree Tree of lnodes.
 * @param[out] yforest Forest of ynodes.
 */
static void
ay_ynode_create_forest(struct ay_lnode *ltree, struct ay_ynode *yforest)
{
    ay_ynode_create_forest_(yforest, ltree);
    ay_ynode_tree_correction(yforest);
    ay_ynode_forest_connect_topnodes(yforest);
    ay_ynode_add_label_value(yforest);
    ay_ynode_add_choice(yforest);
}

/**
 * @brief Copy ynodes to new location.
 *
 * @param[out] dst Destination where are ynode copied.
 * @param[in] src Source where the ynodes are copied from.
 */
static void
ay_ynode_copy(struct ay_ynode *dst, struct ay_ynode *src)
{
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(src); i++) {
        dst[i] = src[i];
        dst[i].parent = AY_MAP_ADDRESS(dst, src, src[i].parent);
        dst[i].next = AY_MAP_ADDRESS(dst, src, src[i].next);
        dst[i].child = AY_MAP_ADDRESS(dst, src, src[i].child);
        LY_ARRAY_INCREMENT(dst);
    }
}

/**
 * @brief Move ynodes in an array one element to the right.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_shift_right(struct ay_ynode *tree)
{
    memmove(&tree[1], tree, LY_ARRAY_COUNT(tree) * sizeof *tree);
    LY_ARRAY_INCREMENT(tree);
    memset(tree, 0, sizeof *tree);

    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        tree[i].parent = tree[i].parent ? tree[i].parent + 1 : NULL;
        tree[i].next = tree[i].next ? tree[i].next + 1 : NULL;
        tree[i].child = tree[i].child ? tree[i].child + 1 : NULL;
    }
}

/**
 * @brief Create ynode tree from ynode forest.
 *
 * @param[in] forest Forest of ynodes.
 * @param[out] tree Resulting ynode tree. Release after use.
 * @return 0 on success.
 */
static int
ay_ynode_create_tree(struct ay_ynode *forest, struct ay_ynode **tree)
{
    struct ay_ynode *iter;
    uint32_t forest_count;

    forest_count = LY_ARRAY_COUNT(forest);

    LY_ARRAY_CREATE(NULL, *tree, 1 + forest_count, return AYE_MEMORY);
    if (!forest_count) {
        return 0;
    }

    ay_ynode_copy(*tree, forest);
    ay_ynode_shift_right(*tree);
    (*tree)->child = (*tree) + 1;
    (*tree)->type = YN_ROOT;

    for (iter = *tree + 1; iter; iter = iter->next) {
        iter->parent = *tree;
        (*tree)->descendants += iter->descendants + 1;
    }

    return 0;
}

/**
 * @brief Check if the repetition (* or +) bound to the @p node.
 *
 * @param[in] node Node to check.
 * @return 1 if repetion affects the node otherwise 0.
 */
static ly_bool
ay_ynode_has_repetition(struct ay_ynode *node)
{
    ly_bool ret = 0;
    struct ay_lnode *iter, *lstart, *lstop;
    struct ay_ynode *yiter, *ystop;

    lstart = node->snode;

    ystop = node;
    for (yiter = node->parent; yiter; yiter = yiter->parent) {
        if (!yiter->snode) {
            break;
        }
        ystop = yiter;
    }
    lstop = ystop == node ? NULL : ystop->snode;

    for (iter = lstart; iter != lstop; iter = iter->parent) {
        if (iter->lens->tag == L_STAR) {
            ret = 1;
            break;
        }
    }

    return ret;
}

/**
 * @brief YN_LIST detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LIST.
 */
static ly_bool
ay_ynode_rule_list(struct ay_ynode *node)
{
    ly_bool has_key;

    has_key = node->label ? node->label->lens->tag == L_KEY : 0;
    return (node->child || has_key) && node->label && ay_ynode_has_repetition(node);
}

/**
 * @brief YN_CONTAINER detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_CONTAINER.
 */
static ly_bool
ay_ynode_rule_container(struct ay_ynode *node)
{
    return node->child && node->label;
}

/**
 * @brief YN_LEAFLIST detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LEAFLIST.
 */
static ly_bool
ay_ynode_rule_leaflist(struct ay_ynode *node)
{
    return !node->child && node->label && ay_ynode_has_repetition(node);
}

/**
 * @brief YN_LEAF detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_LEAF.
 */
static ly_bool
ay_ynode_rule_leaf(struct ay_ynode *node)
{
    return !node->child && node->label;
}

/**
 * @brief YN_KEY detection rule.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode is of type YN_KEY.
 */
static ly_bool
ay_ynode_rule_list_key(struct ay_ynode *node)
{
    struct lens *label, *value;
    ly_bool lab, val;

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    lab = label && AY_TAG_IS_LABEL(label->tag);
    val = value && (value->tag == L_STORE);
    return (node->type == YN_LIST) && (lab || val);
}

/**
 * @brief Check whether the node is to be divided into two.
 *
 * @param[in] node Node to check.
 * @return 1 if ynode must be divided.
 */
static ly_bool
ay_ynode_rule_node_split(struct ay_ynode *node)
{
    struct lens *label, *value;

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    return (node->type == YN_LEAF) && label && (label->tag == L_KEY) && value;
}

/**
 * @brief Check whether the leaf-list node must be changed to list.
 *
 * @param[in] node Node to check.
 * @return 1 if type must be changed.
 */
static ly_bool
ay_ynode_rule_leaflist_to_list(struct ay_ynode *node)
{
    return (node->type == YN_LEAFLIST) && node->label && (node->label->lens->tag == L_SEQ);
}

/**
 * @brief Test ay_ynode_copy().
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] forest Forest of ynodes.
 * @return 0 on success.
 */
static int
ay_ynode_debug_copy(uint64_t vercode, struct ay_ynode *forest)
{
    int ret = 0;
    char *str1, *str2;
    struct ay_ynode *dupl = NULL;
    struct lprinter_ctx_f print_func = {0};

    if (!vercode) {
        return ret;
    }

    LY_ARRAY_CREATE(NULL, dupl, LY_ARRAY_COUNT(forest), return AYE_MEMORY);
    ay_ynode_copy(dupl, forest);

    print_func.main = ay_print_ynode_main;
    print_func.transition = ay_print_ynode_transition;
    ret = ay_print_lens(forest, &print_func, forest->snode->lens, &str1);
    AY_CHECK_RET(ret);

    ret = ay_print_lens(dupl, &print_func, dupl->snode->lens, &str2);
    AY_CHECK_RET(ret);

    ret = ay_print_debug_compare("ynode copy", str1, str2);
    free(str1);
    free(str2);
    LY_ARRAY_FREE(dupl);

    return ret;
}

/**
 * @brief Insert gap in the array.
 *
 * All pointers to ynodes are invalidated.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index where there will be the gap.
 */
static void
ay_ynode_insert_gap(struct ay_ynode *tree, uint32_t index)
{
    memmove(&tree[index + 1], &tree[index], (LY_ARRAY_COUNT(tree) - index) * sizeof *tree);
    memset(&tree[index], 0, sizeof *tree);
    LY_ARRAY_INCREMENT(tree);
}

/**
 * @brief Delete gap in the array.
 *
 * All pointers to ynodes are invalidated.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index where the gap will be deleted.
 */
static void
ay_ynode_delete_gap(struct ay_ynode *tree, uint32_t index)
{
    memmove(&tree[index], &tree[index + 1], (LY_ARRAY_COUNT(tree) - index - 1) * sizeof *tree);
    memset(tree + LY_ARRAY_COUNT(tree) - 1, 0, sizeof *tree);
    LY_ARRAY_DECREMENT(tree);
}

/**
 * @brief Delete node from the tree.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index of the ynode to be deleted.
 */
static void
ay_ynode_delete_node(struct ay_ynode *tree, uint32_t index)
{
    struct ay_ynode *iter;

    for (iter = tree[index].parent; iter; iter = iter->parent) {
        iter->descendants--;
    }
    ay_ynode_delete_gap(tree, index);
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new parent (wrapper) for node.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] index Index of the ynode wrapped.
 */
static void
ay_ynode_insert_wrapper(struct ay_ynode *tree, uint32_t index)
{
    struct ay_ynode *iter, *wrapper;

    for (iter = tree[index].parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    ay_ynode_insert_gap(tree, index);
    wrapper = &tree[index];
    wrapper->descendants = (wrapper + 1)->descendants + 1;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new parent for all children.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] child Index of one of the children to whom a new parent will be inserted.
 */
static void
ay_ynode_insert_parent(struct ay_ynode *tree, uint32_t child)
{
    struct ay_ynode *iter, *parent;
    uint32_t index;

    for (iter = tree[child].parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    index = AY_INDEX(tree, tree[child].parent->child);
    ay_ynode_insert_gap(tree, index);
    parent = &tree[index];
    parent->descendants = (parent - 1)->descendants - 1;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new child for node.
 *
 * New inserted node will be the first child.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] parent Index of the parent who will have new child.
 */
static void
ay_ynode_insert_child(struct ay_ynode *tree, uint32_t parent)
{
    struct ay_ynode *iter, *child;
    uint32_t index;

    for (iter = &tree[parent]; iter; iter = iter->parent) {
        iter->descendants++;
    }
    index = parent + 1;
    ay_ynode_insert_gap(tree, index);
    child = &tree[index];
    child->descendants = 0;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Insert new sibling for node.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] node Index of the node who will have new sibling.
 */
static void
ay_ynode_insert_sibling(struct ay_ynode *tree, uint32_t node)
{
    struct ay_ynode *iter, *sibling;
    uint32_t index;

    for (iter = tree[node].parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
    index = node + tree[node].descendants + 1;
    ay_ynode_insert_gap(tree, index);
    sibling = &tree[index];
    sibling->descendants = 0;
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Move subtree to another place.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Index of the place where the subtree is moved. Gap are inserted on this index.
 * @param[in] src Index to the root of subtree. Gap is deleted after the move.
 */
static void
ay_ynode_move_subtree(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    uint32_t subtree_size;
    struct ay_ynode node;

    if (dst == src) {
        return;
    }

    subtree_size = tree[src].descendants + 1;
    for (uint32_t i = 0; i < subtree_size; i++) {
        node = tree[src];
        ay_ynode_delete_gap(tree, src);
        dst = dst > src ? dst - 1 : dst;
        ay_ynode_insert_gap(tree, dst);
        src = src > dst ? src + 1 : src;
        tree[dst] = node;
        dst++;
    }
}

/**
 * @brief Move subtree to another place as a sibling.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Index of the node whose sibling will be subtree.
 * @param[in] src Index to the root of subtree that moves.
 */
static void
ay_ynode_move_subtree_as_sibling(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    struct ay_ynode *iter;
    uint32_t subtree_size, index;

    if (tree[dst].next == &tree[src]) {
        return;
    }

    subtree_size = tree[src].descendants + 1;
    for (iter = tree[src].parent; iter; iter = iter->parent) {
        iter->descendants -= subtree_size;
    }
    for (iter = tree[dst].parent; iter; iter = iter->parent) {
        iter->descendants += subtree_size;
    }
    index = dst + tree[dst].descendants + 1;
    ay_ynode_move_subtree(tree, index, src);
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Move subtree to another place as a child.
 *
 * @param[in,out] tree Tree of ynodes.
 * @param[in] dst Index of the node whose the first child will be subtree.
 * @param[in] src Index to the root of subtree that moves.
 */
static void
ay_ynode_move_subtree_as_child(struct ay_ynode *tree, uint32_t dst, uint32_t src)
{
    struct ay_ynode *iter;
    uint32_t subtree_size, index;

    if (tree[dst].child == &tree[src]) {
        return;
    }

    subtree_size = tree[src].descendants + 1;
    for (iter = tree[src].parent; iter; iter = iter->parent) {
        iter->descendants -= subtree_size;
    }
    for (iter = &tree[dst]; iter; iter = iter->parent) {
        iter->descendants += subtree_size;
    }
    index = dst + 1;
    ay_ynode_move_subtree(tree, index, src);
    ay_ynode_tree_correction(tree);
}

/**
 * @brief Compare arrays of ynodes and print information if the nodes are different.
 *
 * @param[in] iter Index where to start the comparison.
 * @param[in] arr1 First array of ynodes.
 * @param[in] arr2 Second array of ynodes.
 * @param[in] count Number of elements to compare.
 * @return 0 on success.
 */
static int
ay_ynode_debug_snap(uint32_t iter, struct ay_ynode *arr1, struct ay_ynode *arr2, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (arr1[i].parent != arr2[i].parent) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.parent\n", iter, i);
            return 1;
        } else if (arr1[i].next != arr2[i].next) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.next\n", iter, i);
            return 1;
        } else if (arr1[i].child != arr2[i].child) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.child\n", iter, i);
            return 1;
        } else if (arr1[i].descendants != arr2[i].descendants) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.descendants\n", iter, i);
            return 1;
        } else if (arr1[i].type != arr2[i].type) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.type\n", iter, i);
            return 1;
        } else if (arr1[i].snode != arr2[i].snode) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.snode\n", iter, i);
            return 1;
        } else if (arr1[i].label != arr2[i].label) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.label\n", iter, i);
            return 1;
        } else if (arr1[i].value != arr2[i].value) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.value\n", iter, i);
            return 1;
        } else if (arr1[i].choice != arr2[i].choice) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.choice\n", iter, i);
            return 1;
        } else if (arr1[i].mandatory != arr2[i].mandatory) {
            printf(AY_NAME " DEBUG: iteration %u, diff at node %u ynode.mandatory\n", iter, i);
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Test insert and delete ynode tree operations.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] tree Tree of ynodes. It will not be changed.
 * @return 0 on success.
 */
static int
ay_ynode_debug_insert_delete(uint64_t vercode, struct ay_ynode *tree)
{
    int ret = 0;
    const char *msg;
    struct ay_ynode *dupl = NULL, *snap = NULL;

    if (!vercode) {
        return ret;
    }

    LY_ARRAY_CREATE_GOTO(NULL, dupl, LY_ARRAY_COUNT(tree) + 1, ret, end);
    LY_ARRAY_CREATE_GOTO(NULL, snap, LY_ARRAY_COUNT(tree), ret, end);

    msg = "ynode insert_child";
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        ay_ynode_copy(dupl, tree);
        memcpy(snap, dupl, LY_ARRAY_COUNT(tree) * sizeof *tree);
        ay_ynode_insert_child(dupl, i);
        ay_ynode_delete_node(dupl, i + 1);
        AY_CHECK_GOTO(ay_ynode_debug_snap(0, snap, dupl, LY_ARRAY_COUNT(tree)), error);
        AY_SET_LY_ARRAY_SIZE(dupl, 0);
    }

    msg = "ynode insert_wrapper";
    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ay_ynode_copy(dupl, tree);
        memcpy(snap, dupl, LY_ARRAY_COUNT(tree) * sizeof *tree);
        ay_ynode_insert_wrapper(dupl, i);
        ay_ynode_delete_node(dupl, i);
        AY_CHECK_GOTO(ay_ynode_debug_snap(0, snap, dupl, LY_ARRAY_COUNT(tree)), error);
        AY_SET_LY_ARRAY_SIZE(dupl, 0);
    }

    msg = "ynode insert_parent";
    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ay_ynode_copy(dupl, tree);
        memcpy(snap, dupl, LY_ARRAY_COUNT(tree) * sizeof *tree);
        ay_ynode_insert_parent(dupl, i);
        ay_ynode_delete_node(dupl, AY_INDEX(dupl, dupl[i + 1].parent));
        AY_CHECK_GOTO(ay_ynode_debug_snap(0, snap, dupl, LY_ARRAY_COUNT(tree)), error);
        AY_SET_LY_ARRAY_SIZE(dupl, 0);
    }

    msg = "ynode insert_sibling";
    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ay_ynode_copy(dupl, tree);
        memcpy(snap, dupl, LY_ARRAY_COUNT(tree) * sizeof *tree);
        ay_ynode_insert_sibling(dupl, i);
        ay_ynode_delete_node(dupl, AY_INDEX(dupl, dupl[i].next));
        AY_CHECK_GOTO(ay_ynode_debug_snap(0, snap, dupl, LY_ARRAY_COUNT(tree)), error);
        AY_SET_LY_ARRAY_SIZE(dupl, 0);
    }

end:
    if (ret) {
        printf(AY_NAME " DEBUG: %s failed\n", msg);
    }
    LY_ARRAY_FREE(dupl);
    LY_ARRAY_FREE(snap);

    return ret;

error:
    ret = AYE_DEBUG_FAILED;
    goto end;
}

/**
 * @brief Test move ynode subtree operations.
 *
 * @param[in] vercode Verbose that decides the execution of a function.
 * @param[in] tree Tree of ynodes. It will not be changed.
 * @return 0 on successs.
 */
static int
ay_ynode_debug_move_subtree(uint64_t vercode, struct ay_ynode *tree)
{
    int ret = 0;
    struct ay_ynode *dupl = NULL, *snap = NULL, *place;
    const char *msg;

    if (!vercode) {
        return ret;
    }

    LY_ARRAY_CREATE_GOTO(NULL, dupl, LY_ARRAY_COUNT(tree), ret, end);
    LY_ARRAY_CREATE_GOTO(NULL, snap, LY_ARRAY_COUNT(tree), ret, end);
    ay_ynode_copy(dupl, tree);
    memcpy(snap, dupl, LY_ARRAY_COUNT(tree) * sizeof *tree);

    msg = "ynode move_subtree_as_sibling";
    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree) - 1; i++) {
        if (dupl[i].next && dupl[i].next->next) {
            ay_ynode_move_subtree_as_sibling(dupl, i, AY_INDEX(dupl, dupl[i].next->next));
            place = dupl[i].next->next ? dupl[i].next->next : dupl[i].next + dupl[i].next->descendants + 1;
            ay_ynode_move_subtree_as_sibling(dupl, AY_INDEX(dupl, place), AY_INDEX(dupl, dupl[i].next));
            AY_CHECK_GOTO(ay_ynode_debug_snap(0, snap, dupl, LY_ARRAY_COUNT(tree)), error);
        }
    }

    msg = "ynode move_subtree_as_child";
    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree) - 1; i++) {
        if (dupl[i].next && dupl[i].next->child) {
            ay_ynode_move_subtree_as_child(dupl, i, AY_INDEX(dupl, dupl[i].next->child));
            ay_ynode_move_subtree_as_child(dupl, AY_INDEX(dupl, dupl[i].next), AY_INDEX(dupl, dupl[i].child));
            AY_CHECK_GOTO(ay_ynode_debug_snap(0, snap, dupl, LY_ARRAY_COUNT(tree)), error);
        }
    }

end:
    if (ret) {
        printf(AY_NAME " DEBUG: %s failed\n", msg);
    }
    LY_ARRAY_FREE(dupl);
    LY_ARRAY_FREE(snap);

    return ret;

error:
    ret = AYE_DEBUG_FAILED;
    goto end;
}

/**
 * @brief Check if the 'maybe' operator (?) is bound to the @p node.
 *
 * @param[in] node Node to check.
 * @return 1 if maybe operator affects the node otherwise 0.
 */
static ly_bool
ay_lnode_has_maybe(struct ay_lnode *node)
{
    struct ay_lnode *iter;

    if (!node) {
        return 0;
    }

    for (iter = node->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
        if (iter->lens->tag == L_MAYBE) {
            return 1;
        }
    }

    if (iter) {
        for (iter = iter->parent; iter && (iter->lens->tag != L_SUBTREE); iter = iter->parent) {
            if (iter->lens->tag == L_MAYBE) {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Set ynode.mandatory for node.
 *
 * @param[in,out] node Node whose mandatory statement will be set.
 */
static void
ay_ynode_set_mandatory(struct ay_ynode *node)
{
    if (ay_lnode_has_maybe(node->label) || ay_lnode_has_maybe(node->value)) {
        node->mandatory = 0;
    } else {
        node->mandatory = 1;
    }
}

/**
 * @brief Set ynode.mandatory for all nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_tree_set_mandatory(struct ay_ynode *tree)
{
    assert(tree && ((tree[0].type == YN_UNKNOWN) || (tree[0].type == YN_ROOT)));

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        if ((tree[i].type != YN_LIST) && (tree[i].type != YN_LEAFLIST)) {
            ay_ynode_set_mandatory(&tree[i]);
        }
    }
}

/**
 * @brief Set ynode.min for all list and leaf-list nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_tree_set_min(struct ay_ynode *tree)
{
    struct ay_ynode *node;

    /* The value is set by ::ay_ynode_delete_build_list() and this code unset the value due to the '?' operator. */
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if ((node->type == YN_LIST) || (node->type == YN_LEAFLIST)) {
            if (ay_lnode_has_maybe(node->snode)) {
                node->min = 0;
            }
        }
    }
}

/**
 * @brief If L_LABEL is more appropriate for key than L_KEY, change key to YN_INDEX.
 *
 * Sometimes the author of the Augeas module chooses the wrong lense primitive and writes 'key' instead of 'label'.
 * As a result, no more than one record can be entered in the yang list. So the key type must be changed to YN_INDEX
 * for meaningful use of the yang list.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_key_pattern_to_data_path(struct ay_ynode *tree)
{
    struct ay_ynode *node, *key;
    struct lens *label;
    char *ch;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        label = AY_LABEL_LENS(node);

        /* select list that has L_KEY */
        if ((node->type != YN_LIST) || !label || (label->tag != L_KEY)) {
            continue;
        }

        /* check if regexp pattern is simple a-z A-Z string */
        for (ch = label->regexp->pattern->str; *ch != '\0'; ch++) {
            if (((*ch < 65) && (*ch != 45)) || ((*ch > 90) && (*ch != 95) && (*ch < 97)) || (*ch > 122)) {
                break;
            }
        }
        if (*ch != '\0') {
            break;
        }

        /* get key ynode and change type to YN_INDEX */
        for (key = node->child; key->type != YN_KEY; key = key->next) {}
        key->type = YN_INDEX;
    }
}

/**
 * @brief Delete nodes with unkown type.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_type_unknown(struct ay_ynode *tree)
{
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        if ((tree[i].type == YN_UNKNOWN) && (!tree[i].child)) {
            ay_ynode_delete_node(tree, i);
            i--;
        }
    }
}

/**
 * @brief Delete generally using nodes for comments.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_comment(struct ay_ynode *tree)
{
    struct ay_ynode *iter;
    struct lens *label;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        iter = &tree[i];
        label = AY_LABEL_LENS(iter);
        if (label && (label->tag == L_LABEL)) {
            if (!strcmp("#comment", label->string->str)) {
                ay_ynode_delete_node(tree, i);
                i--;
            }
        }
    }
}

/**
 * @brief Delete choice for top-nodes.
 *
 * It is typically undesirable.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_top_choice(struct ay_ynode *tree)
{
    struct ay_ynode *iter;

    /* remove choice */
    for (iter = tree->child; iter; iter = iter->next) {
        iter->choice = NULL;
    }
}

/**
 * @brief Delete choice for top-nodes.
 *
 * Delete "lns . ( sep . lns )*" pattern (TODO bilateral). This pattern is located in Build module (build.aug).
 * The first 'lns' is useless.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_build_list(struct ay_ynode *tree)
{
    struct ay_ynode *node1, *node2;
    struct ay_lnode *iter1, *iter2, *concat;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node1 = &tree[i];
        for (uint32_t j = i + 1; j < LY_ARRAY_COUNT(tree); j++) {
            node2 = &tree[j];

            /* node1 == node2 */
            if (!node1->snode || !node2->snode || (node1->snode->lens != node2->snode->lens)) {
                continue;
            }

            /* node1 and node2 have the same concat operator */
            concat = NULL;
            for (iter1 = node1->snode->parent; iter1 && !concat; iter1 = iter1->parent) {
                if (iter1->lens->tag != L_CONCAT) {
                    continue;
                }
                for (iter2 = node2->snode->parent; iter2 && !concat; iter2 = iter2->parent) {
                    if (iter2->lens->tag != L_CONCAT) {
                        continue;
                    }
                    concat = iter1 == iter2 ? iter1 : NULL;
                }
            }
            if (!concat) {
                continue;
            }

            /* node1 should not have an asterisk */
            for (iter1 = node1->snode->parent; (iter1 != concat) && (iter1->lens->tag != L_STAR);
                    iter1 = iter1->parent) {}
            if (iter1->lens->tag == L_STAR) {
                continue;
            }

            /* node2 should have an asterisk */
            for (iter2 = node2->snode->parent; (iter2 != concat) && (iter2->lens->tag != L_STAR);
                    iter2 = iter2->parent) {}
            if (iter2->lens->tag != L_STAR) {
                continue;
            }

            /* set minimal-elements for node2 */
            node2->min++;

            /* delete node1 because it is useless */
            ay_ynode_delete_node(tree, AY_INDEX(tree, node1));
            i--;
            break;
        }
    }
}

/**
 * @brief Delete lonely list key which is unnecessary.
 *
 * Delete "[key lns1 store lns2] | [key lns1]" pattern (bilateral).
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_delete_lonely_key(struct ay_ynode *tree)
{
    struct ay_ynode *iter, *node;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if (!node->choice || !node->label || (node->label->lens->tag != L_KEY)) {
            continue;
        }

        /* node and node2 must have the same choice_id */
repeat:
        for (iter = node->next; iter; iter = iter->next) {
            if (!iter->choice || !iter->label || (iter->label->lens->tag != L_KEY)) {
                continue;
            }
            if (node->choice != iter->choice) {
                continue;
            }
            if (node->label->lens->regexp != iter->label->lens->regexp) {
                continue;
            }
            if (!node->value && (iter->value->lens->tag == L_STORE)) {
                iter->mandatory = 0;
                ay_ynode_delete_node(tree, AY_INDEX(tree, node));
                i--;
                break;
            } else if (!iter->value && (node->value->lens->tag == L_STORE)) {
                node->mandatory = 0;
                ay_ynode_delete_node(tree, AY_INDEX(tree, iter));
                goto repeat;
            }
        }
    }
}

/**
 * @brief Unify the lists under the choice relationship if they have the same key.
 *
 * If list's are in choice relation and they have same key then there will be one list and its nodes will
 * have that choice relation.
 * Transform "[key lns1 {some_nodes1} ] | [key lns1 {some_nodes2}]" -> "[key lns1 {some_nodes1 | some_nodes2}]"
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_list_with_same_key(struct ay_ynode *tree)
{
    struct ay_ynode *iter, *list1, *list2, *cont1, *cont2, *node;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        if ((tree[i].type != YN_LIST) || (!tree[i].choice) || (!tree[i].label)) {
            continue;
        }
        list1 = &tree[i];

        for (iter = list1->next; iter && (iter->choice == list1->choice); iter = iter->next) {
            if ((tree[i].type != YN_LIST) || (!tree[i].label)) {
                continue;
            }
            list2 = iter;

            if (list1->label->lens != list2->label->lens) {
                continue;
            }

            /* delete list2 keys */
            for (uint32_t j = 0; j < list2->descendants; j++) {
                node = &list2[j + 1];
                if (AY_TYPE_LIST_KEY(node)) {
                    ay_ynode_delete_node(tree, AY_INDEX(tree, node));
                    j--;
                }
            }

            /* change list2 to container */
            cont2 = list2;
            cont2->type = YN_CONTAINER;
            cont2->label = cont2->value = NULL;

            /* insert container for list1 nodes */
            ay_ynode_insert_parent(tree, AY_INDEX(tree, list1->child));
            cont2++;
            cont1 = list1->child;
            cont1->type = YN_CONTAINER;
            /* set cont1 the same choice */
            cont1->choice = cont2->choice;
            list1->choice = NULL;

            /* move keys from cont1 to list1 */
            for (uint32_t j = 0; j < list1->descendants; j++) {
                node = &list1[j + 1];
                if (AY_TYPE_LIST_KEY(node)) {
                    ay_ynode_move_subtree_as_child(tree, AY_INDEX(tree, list1), AY_INDEX(tree, node));
                    j--;
                    cont1++;
                }
            }

            /* move cont2 next to cont1 */
            ay_ynode_move_subtree_as_sibling(tree, AY_INDEX(tree, cont1), AY_INDEX(tree, cont2));

            /* change label pointer for YN_VALUE */
            for (uint32_t j = 0; j < cont2->descendants; j++) {
                node = &cont2[j + 1];
                if (node->type == YN_VALUE) {
                    assert(AY_TYPE_LIST_KEY(list1->child));
                    node->label = list1->child->label;
                }
            }
        }
    }
}

/**
 * @brief Delete the containers that has only one child.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_delete_poor_container(struct ay_ynode *tree)
{
    struct ay_ynode *node;
    uint32_t i;

    assert((tree->descendants > 1) && (tree[0].type == YN_ROOT));

    for (i = (tree[1].type == YN_CONTAINER) ? 2 : 1; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];

        if (node->type == YN_CONTAINER) {
            assert(node->child);
            if (!node->child->next) {
                node->child->choice = node->choice;
                ay_ynode_delete_node(tree, AY_INDEX(tree, node));
                i--;
            }
        }
    }
}

/**
 * @brief Insert a sheet whose key is the path to the configuration file that Augeas parsed.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_insert_list_files(struct ay_ynode *tree)
{
    struct ay_ynode *list;

    ay_ynode_insert_parent(tree, AY_YNODE_LIST_FILES_INDEX);
    list = &tree[AY_YNODE_LIST_FILES_INDEX];
    list->type = YN_LIST;
}

/**
 * @brief insert list key.
 *
 * also node with 'store' pattern can be generated too.
 *
 * @param[in,out] tree tree of ynodes.
 */
static void
ay_insert_list_key(struct ay_ynode *tree)
{
    struct ay_ynode *parent;
    struct lens *label, *value;
    uint32_t parent_idx;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        parent = &tree[i];
        parent_idx = i;
        if (!ay_ynode_rule_list_key(parent)) {
            continue;
        }
        label = AY_LABEL_LENS(parent);
        value = AY_VALUE_LENS(parent);

        if (!value && label && (label->tag == L_LABEL)) {
            ay_ynode_insert_child(tree, parent_idx);
            tree[parent_idx + 1].type = YN_INDEX;
            tree[parent_idx + 1].label = parent->label;
            i++;
            continue;
        } else if (value && (value->tag == L_STORE) && label && (label->tag == L_LABEL)) {
            ay_ynode_insert_child(tree, parent_idx);
            tree[parent_idx + 1].type = YN_KEY;
            tree[parent_idx + 1].label = parent->label;
            tree[parent_idx + 1].value = parent->value;
            i++;
            continue;
        } else if (value && (value->tag == L_STORE)) {
            ay_ynode_insert_child(tree, parent_idx);
            tree[parent_idx + 1].type = YN_VALUE;
            tree[parent_idx + 1].label = parent->label;
            tree[parent_idx + 1].value = parent->value;
            ay_ynode_set_mandatory(&tree[parent_idx + 1]);
            i++;
        }

        if (label) {
            ay_ynode_insert_child(tree, parent_idx);
            tree[parent_idx + 1].type = YN_KEY;
            tree[parent_idx + 1].label = parent->label;
            tree[parent_idx + 1].value = parent->value;
            i++;
        }
    }
}

/**
 * @brief Split node if has two patterns (L_KEY and L_STORE).
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_node_split(struct ay_ynode *tree)
{
    struct ay_ynode *node, *valnode;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if (ay_ynode_rule_node_split(node)) {
            // TODO if node has choice -> insert container, also L_VALUE case
            assert(!node->choice && (node->value->lens->tag == L_STORE));
            ay_ynode_insert_sibling(tree, AY_INDEX(tree, node));
            valnode = node->next;
            valnode->type = YN_VALUE;
            valnode->label = node->label;
            valnode->value = node->value;
            ay_ynode_set_mandatory(valnode);
            i = AY_INDEX(tree, valnode);
        }
    }
}

/**
 * @brief Change leaflist to list, add key and optionally YN_VALUE node.
 *
 * The leaf-list must change to list, if numbers itself should be labels for Augeas nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_leaflist_to_list(struct ay_ynode *tree)
{
    struct ay_ynode *list, *key, *valnode;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        if (ay_ynode_rule_leaflist_to_list(&tree[i])) {
            list = &tree[i];
            list->type = YN_LIST;
            if (list->value) {
                ay_ynode_insert_child(tree, i);
                valnode = list->child;
                valnode->type = YN_VALUE;
                valnode->label = list->label;
                valnode->value = list->value;
            }
            ay_ynode_insert_child(tree, i);
            key = list->child;
            key->type = YN_KEY;
            key->label = list->label;
            key->value = list->value;
        }
    }
}

/**
 * @brief Set ay_ynode.type for all nodes.
 *
 * @param[in,out] tree Tree of ynodes.
 */
static void
ay_ynode_set_type(struct ay_ynode *tree)
{
    struct ay_ynode *node;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(tree); i++) {
        node = &tree[i];
        if (!node->snode) {
            assert((node->type != YN_UNKNOWN) || (node->type == YN_ROOT));
            continue;
        }

        if (ay_ynode_rule_list(node)) {
            node->type = YN_LIST;
        } else if (ay_ynode_rule_container(node)) {
            node->type = YN_CONTAINER;
        } else if (ay_ynode_rule_leaflist(node)) {
            node->type = YN_LEAFLIST;
        } else if (ay_ynode_rule_leaf(node)) {
            node->type = YN_LEAF;
        }
    }
}

/**
 * @brief Wrapper for calling some insert function.
 *
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * The insertion result will be applied.
 * @param[in] rule Callback function for @p insert function. To insert a node, the rule returns 1.
 * @param[in] insert Callback function which inserts some nodes.
 * @param[in] counter_multiplier The total number of complied rules (how often @p rule returns 1)
 * is multiplied by the number @p counter_multiplier.
 * @return 1 on success.
 */
static int
ay_ynode_trans_insert1(struct ay_ynode **tree, ly_bool (*rule)(struct ay_ynode *), void (*insert)(struct ay_ynode *),
        uint32_t counter_multiplier)
{
    uint32_t counter = 0;
    struct ay_ynode *new = NULL;

    ay_ynode_summary(*tree, rule, &counter);
    LY_ARRAY_CREATE(NULL, new, (counter_multiplier * counter) + LY_ARRAY_COUNT(*tree), return AYE_MEMORY);
    ay_ynode_copy(new, *tree);
    insert(new);
    LY_ARRAY_FREE(*tree);
    *tree = new;

    return 0;
}

/**
 * @brief Wrapper for calling some insert function.
 *
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * The insertion result will be applied.
 * @param[in] items_count Number of nodes to be inserted.
 * @param[in] insert Callback function which inserts some nodes.
 * @return 0 on success.
 */
static int
ay_ynode_trans_insert2(struct ay_ynode **tree, uint32_t items_count, void (*insert)(struct ay_ynode *))
{
    struct ay_ynode *new = NULL;

    LY_ARRAY_CREATE(NULL, new, items_count + LY_ARRAY_COUNT(*tree), return AYE_MEMORY);
    ay_ynode_copy(new, *tree);
    insert(new);
    LY_ARRAY_FREE(*tree);
    *tree = new;

    return 0;
}

/**
 * @brief Apply various transformations before the tree is ready to print.
 *
 * @param[in] vercode Verbose that decides the execution of a debug functions.
 * @param[in,out] tree Tree of ynodes. The memory address of the tree will be changed.
 * @return 0 on success.
 */
static int
ay_ynode_transformations(uint64_t vercode, struct ay_ynode **tree)
{
    int ret = 0;

    assert((*tree)->type == YN_ROOT);

    /* delete "lns . ( sep . lns )*" pattern (TODO bilateral) */
    ay_ynode_delete_build_list(*tree);

    /* delete "[key lns1 store lns2] | [key lns1]" pattern (bilateral) */
    ay_ynode_delete_lonely_key(*tree);

    /* set type */
    ay_ynode_set_type(*tree);

    /* set mandatory */
    ay_ynode_tree_set_mandatory(*tree);

    /* set minimal-elements */
    ay_ynode_tree_set_min(*tree);

    /* delete unnecessary nodes */
    ay_delete_type_unknown(*tree);
    ay_delete_comment(*tree);
    ay_delete_top_choice(*tree);

    ret = ay_ynode_debug_tree(vercode, AYV_TRANS_REMOVE, *tree);
    AY_CHECK_RET(ret);

    ret = ay_ynode_trans_insert2(tree, 1, ay_insert_list_files);
    AY_CHECK_RET(ret);
    ret = ay_ynode_trans_insert1(tree, ay_ynode_rule_list_key, ay_insert_list_key, 2);
    AY_CHECK_RET(ret);
    ret = ay_ynode_trans_insert1(tree, ay_ynode_rule_node_split, ay_node_split, 1);
    AY_CHECK_RET(ret);
    ret = ay_ynode_trans_insert1(tree, ay_ynode_rule_leaflist_to_list, ay_ynode_leaflist_to_list, 2);
    AY_CHECK_RET(ret);

    ay_ynode_key_pattern_to_data_path(*tree);

    ret = ay_ynode_debug_tree(vercode, AYV_TRANS_INSERT1, *tree);
    AY_CHECK_RET(ret);

    ay_delete_list_with_same_key(*tree);
    ay_delete_poor_container(*tree);

    return ret;
}

int
augyang_print_yang(struct module *mod, uint64_t vercode, char **str)
{
    int ret = 0;
    struct lens *lens;
    struct ay_lnode *ltree = NULL;
    struct ay_ynode *yforest = NULL, *ytree = NULL;
    uint32_t ltree_size = 0, yforest_size = 0;
    ly_bool l_rec = 0;

    lens = ay_lense_get_root(mod);
    AY_CHECK_COND(!lens, AYE_LENSE_NOT_FOUND);

    ay_lense_summary(lens, &ltree_size, &yforest_size, &l_rec);
    AY_CHECK_COND(l_rec, AYE_L_REC);

    /* Create lnode tree. */
    LY_ARRAY_CREATE_GOTO(NULL, ltree, ltree_size, ret, end);
    ay_lnode_create_tree(ltree, lens, ltree);
    ay_lnode_debug_tree(vercode, mod, ltree);

    /* Create ynode forest. */
    LY_ARRAY_CREATE_GOTO(NULL, yforest, yforest_size, ret, end);
    ay_ynode_create_forest(ltree, yforest);
    ay_ynode_debug_forest(vercode, mod, yforest);

    /* Convert ynode forest to tree. */
    ret = ay_ynode_debug_copy(vercode, yforest);
    AY_CHECK_GOTO(ret, end);
    ret = ay_ynode_create_tree(yforest, &ytree);
    AY_CHECK_GOTO(ret, end);
    ret = ay_ynode_debug_tree(vercode, AYV_YTREE, ytree);
    AY_CHECK_GOTO(ret, end);

    /* Apply transformations. */
    ret = ay_ynode_debug_insert_delete(vercode, ytree);
    AY_CHECK_GOTO(ret, end);
    ret = ay_ynode_debug_move_subtree(vercode, ytree);
    AY_CHECK_GOTO(ret, end);
    ret = ay_ynode_transformations(vercode, &ytree);
    AY_CHECK_GOTO(ret, end);
    ret = ay_ynode_debug_tree(vercode, AYV_YTREE_AFTER_TRANS, ytree);
    AY_CHECK_GOTO(ret, end);

    ret = ay_print_yang(mod, ytree, str);

end:
    LY_ARRAY_FREE(ltree);
    LY_ARRAY_FREE(yforest);
    LY_ARRAY_FREE(ytree);

    return ret;
}
