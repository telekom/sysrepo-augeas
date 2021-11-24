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

/**
 * @brief Maximum size of regular expression (yang statement pattern).
 */
#define AY_MAX_REGEX_SIZE 128

/* error codes */

#define AYE_MEMORY 1
#define AYE_LENSE_NOT_FOUND 2
#define AYE_L_REC 3
#define AYE_DEBUG_FAILED 4
#define AYE_IDENT_NOT_FOUND 5
#define AYE_IDENT_LIMIT 6
#define AYE_REGEX_LIMIT 7

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
    YN_KEY,             /**< Yang statement "leaf". Also indicates that node is a key in the yang "list". */
    YN_ROOT             /**< A special type that is only one in the ynode tree. Indicates the root of the entire tree.
                             It has no printing application only makes writing algorithms easier. */
};

/**
 * @brief Node for printing the yang node.
 *
 * The ynode node represents the yang data node. It is generally created from a lense with tag L_SUBTREE, but can also
 * be created later, in which case ay_ynode.snode is set to NULL. Nodes are stored in the Sized array, so if a node is
 * added or removed from the array, then the ay_ynode pointers must be reset to the correct address so that the tree
 * structure made sense. Functions ay_ynode_insert_* and ay_ynode_remove_node() are intended for these modifications.
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
 * So, for example, if a node has multiple labels, the ay_lnode_next_lv() returns the next one. (TODO)
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
    uint32_t space;         /**< Current indent. */
    struct module *mod;     /**< Augeas module. */
    struct ay_ynode *tree;  /**< Pointer to the Sized array. */
    struct ly_out *out;     /**< Output to which it is printed. */
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
    case AYE_REGEX_LIMIT:
        return AY_NAME " ERROR: regex string is too long. Output YANG is not valid.\n";
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
    (*ltree_size)++;
    *yforest_size = lens->tag == L_SUBTREE ?
            *yforest_size + 1 :
            *yforest_size;
    *l_rec |= lens->tag == L_REC;

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

static void
ay_ynode_summary(struct ay_ynode *forest, ly_bool (*rule)(struct ay_ynode *), uint32_t *cnt)
{
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        if (rule(&forest[i])) {
            (*cnt)++;
        }
    }
}

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
            ctx->func.transition(ctx);
            break;
        case L_STORE:
            ay_print_lens_node_header(out, lens, sp, "L_STORE");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_store_regex: %s\n", sp, "", regex);
            free(regex);
            ctx->func.transition(ctx);
            break;
        case L_VALUE:
            ay_print_lens_node_header(out, lens, sp, "L_VALUE");
            ly_print(out, "%*s lens_value_string: %s\n", sp, "", lens->string->str);
            ctx->func.transition(ctx);
            break;
        case L_KEY:
            ay_print_lens_node_header(out, lens, sp, "L_KEY");
            regex = regexp_escape(lens->regexp);
            ly_print(out, "%*s lens_key_regex: %s\n", sp, "", regex);
            free(regex);
            ctx->func.transition(ctx);
            break;
        case L_LABEL:
            ay_print_lens_node_header(out, lens, sp, "L_LABEL");
            ly_print(out, "%*s lens_label_string: %s\n", sp, "", lens->string->str);
            ctx->func.transition(ctx);
            break;
        case L_SEQ:
            ay_print_lens_node_header(out, lens, sp, "L_SEQ");
            ly_print(out, "%*s lens_seq_string: %s\n", sp, "", lens->string->str);
            ctx->func.transition(ctx);
            break;
        case L_COUNTER:
            ay_print_lens_node_header(out, lens, sp, "L_COUNTER");
            ly_print(out, "%*s lens_counter_string: %s\n", sp, "", lens->string->str);
            ctx->func.transition(ctx);
            break;
        case L_CONCAT:
            ay_print_lens_node_header(out, lens, sp, "L_CONCAT");
            ctx->func.transition(ctx);
            break;
        case L_UNION:
            ay_print_lens_node_header(out, lens, sp, "L_UNION");
            ctx->func.transition(ctx);
            break;
        case L_SUBTREE:
            ay_print_lens_node_header(out, lens, sp, "L_SUBTREE");
            ctx->func.transition(ctx);
            break;
        case L_STAR:
            ay_print_lens_node_header(out, lens, sp, "L_STAR");
            ctx->func.transition(ctx);
            break;
        case L_MAYBE:
            ay_print_lens_node_header(out, lens, sp, "L_MAYBE");
            ctx->func.transition(ctx);
            break;
        case L_REC:
            ay_print_lens_node_header(out, lens, sp, "L_REC");
            ly_print(out, "ay_print_lens_node error: L_REC not supported\n");
            break;
        case L_SQUARE:
            ay_print_lens_node_header(out, lens, sp, "L_SQUARE");
            ctx->func.transition(ctx);
            break;
        default:
            ly_print(out, "ay_print_lens_node error\n");
            break;
        }
    } else {
        ctx->func.transition(ctx);
    }

    sp -= SPACE_INDENT;
    ly_print(out, "%*s }\n", sp, "");
    ctx->space = sp;
}

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

static void
ay_print_yang_module_name(struct yprinter_ctx *ctx)
{
    char *name;
    size_t namelen;
    struct lens *lens;

    lens = ay_lense_get_root(ctx->mod);
    ay_get_filename(lens->info->filename->str, &name, &namelen);

    ly_print(ctx->out, "%.*s", namelen, name);
}

static void
ay_print_yang_nesting_begin(struct yprinter_ctx *ctx)
{
    ly_print(ctx->out, " {\n");
    ctx->space += SPACE_INDENT;
}

static void
ay_print_yang_nesting_end(struct yprinter_ctx *ctx)
{
    ctx->space -= SPACE_INDENT;
    ly_print(ctx->out, "%*s}\n", ctx->space, "");
}

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

struct regex_map
{
    const char *aug;
    const char *yang;
};

static int
ay_get_regex_standardized(const struct regexp *rp, char *buffer)
{
    char *regex = NULL;
    char *hit = NULL;
    uint32_t cnt;

    struct regex_map regmap[] = {
        {"[_.A-Za-z0-9][-_.A-Za-z0-9]*\\\\$?", "[_.A-Za-z0-9][-_.A-Za-z0-9]*"},
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
            strcpy(buffer, regmap[i].yang);
            goto end;
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

    strcpy(buffer, regex);

end:
    free(regex);

    return 0;
}

static int
ay_get_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node, char *buffer)
{
    int ret = 0;
    char *str = NULL;
    struct lens *label, *value, *snode;

    snode = AY_SNODE_LENS(node);
    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);

    if (node->type == YN_KEY) {
        if (!label && !value) {
            str = (char *)"key";
        } else if (!label && value && (value->tag == L_STORE)) {
            str = ay_get_lense_name(ctx->mod, value);
            str = !str ? (char *)"value" : str;
        } else if (label->tag == L_KEY) {
            str = ay_get_lense_name(ctx->mod, label);
            str = !str ? (char *)"key" : str;
        } else if (label->tag == L_SEQ) {
            str = (char *)"key";
        } else if (label->tag == L_LABEL) {
            str = label->string->str;
        } else {
            return AYE_IDENT_NOT_FOUND;
        }
    } else if (node->type == YN_LIST) {
        str = ay_get_lense_name(ctx->mod, snode);
        if (!str) {
            if (label->tag == L_KEY) {
                str = ay_get_lense_name(ctx->mod, label);
            } else if (label->tag == L_SEQ) {
                str = label->string->str;
            } else if (label->tag == L_LABEL) {
                str = label->string->str;
            }
            str = !str ? (char *)"list" : str;
        }
    } else {
        if (!label && value && (value->tag == L_STORE)) {
            str = ay_get_lense_name(ctx->mod, value);
        } else if (label && (label->tag == L_KEY)) {
            str = ay_get_lense_name(ctx->mod, label);
        } else if (label && (label->tag == L_SEQ)) {
            str = label->string->str;
        } else if (label && (label->tag == L_LABEL)) {
            str = label->string->str;
        }

        if (!label || !str) {
            str = ay_get_lense_name(ctx->mod, snode);
            str = !str ? (char *)"node" : str;
        }
    }

    AY_CHECK_COND(!str, ret);
    ay_get_ident_standardized(str, buffer);

    return ret;
}

static int
ay_print_yang_ident(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    char ident[AY_MAX_IDENT_SIZE];
    char siblings_ident[AY_MAX_IDENT_SIZE];
    struct ay_ynode *iter_start, *iter;
    uint32_t duplicates = 1;

    if (node->parent && (node->parent->type == YN_ROOT)) {
        ay_print_yang_module_name(ctx);
        return ret;
    }

    ret = ay_get_yang_ident(ctx, node, ident);
    AY_CHECK_RET(ret);

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

static void
ay_print_yang_data_path_item_key(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    struct ay_ynode *iter;

    for (iter = node->parent->child; iter; iter = iter->next) {
        if (iter->type == YN_KEY) {
            // TODO: more keys?
            ly_print(ctx->out, "$");
            ay_print_yang_ident(ctx, iter);
            break;
        }
    }
}

static ly_bool
ay_print_yang_data_path_item(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    ly_bool ret = 0;
    struct lens *parent_label = NULL, *label = NULL;

    if (node->parent) {
        parent_label = AY_LABEL_LENS(node->parent);
    }
    label = AY_LABEL_LENS(node);

    if (node->type == YN_LIST) {
        /* list is ignored */
    } else if (node->parent && (node->type == YN_CONTAINER) && (node->parent->type == YN_ROOT)) {
        /* top-level data-container */
    } else if (node->parent && (node->parent->type == YN_LIST) && (node->type == YN_KEY)) {
        ly_print(ctx->out, "$");
        ay_print_yang_ident(ctx, node);
    } else if (node->parent && (node->parent->type == YN_LIST) &&
            parent_label && (parent_label->tag == L_LABEL)) {
        ly_print(ctx->out, "%s", parent_label->string->str);
        ly_print(ctx->out, "/");
        ay_print_yang_ident(ctx, node);
    } else if (node->parent && (node->parent->type == YN_LIST) &&
            (node->value) && (node->value == node->parent->value)) {
        ay_print_yang_data_path_item_key(ctx, node);
    } else if (node->parent && (node->parent->type == YN_LIST) && (node->type == YN_LEAFLIST) &&
            label && (label->tag == L_LABEL)) {
        ay_print_yang_data_path_item_key(ctx, node);
        ly_print(ctx->out, "/");
        ly_print(ctx->out, "%s", label->string->str);
    } else if (node->parent && (node->parent->type == YN_LIST) && (node->type == YN_LEAFLIST)) {
        ay_print_yang_data_path_item_key(ctx, node);
        ly_print(ctx->out, "/$AY_UINT");
    } else if (node->type == YN_LEAFLIST) {
        ly_print(ctx->out, "$AY_UINT");
    } else if (node->parent && (node->parent->type == YN_LIST)) {
        ay_print_yang_data_path_item_key(ctx, node);
        ly_print(ctx->out, "/");
        ay_print_yang_ident(ctx, node);
    } else {
        ay_print_yang_ident(ctx, node);
        ret = 1;
    }

    return ret;
}

static void
ay_print_yang_data_path_r(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    ly_bool ret;

    if (!node || (node->type == YN_ROOT)) {
        return;
    }

    ay_print_yang_data_path_r(ctx, node->parent);
    ret = ay_print_yang_data_path_item(ctx, node);
    if (ret) {
        ly_print(ctx->out, "/");
    }
}

static void
ay_print_yang_data_path(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->parent && (node->type == YN_CONTAINER) && (node->parent->type == YN_ROOT)) {
        /* top-level data-container */
        return;
    } else if (node->type == YN_LIST) {
        return;
    }
    ly_print(ctx->out, "%*s"AY_EXT_PREFIX ":"AY_EXT_PATH " \"", ctx->space, "");
    ay_print_yang_data_path_r(ctx, node->parent);
    ay_print_yang_data_path_item(ctx, node);
    ly_print(ctx->out, "\";\n");
}

static int
ay_print_yang_type(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    const struct regexp *rp;
    char buffer[AY_MAX_REGEX_SIZE];
    struct lens *label, *value;

    if (!node->label && !node->value) {
        return ret;
    }

    ly_print(ctx->out, "%*stype string", ctx->space, "");

    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if (label && (label->tag == L_KEY)) {
        rp = label->regexp;
    } else if (value && (value->tag == L_STORE)) {
        rp = value->regexp;
    } else {
        ly_print(ctx->out, ";\n");
        return ret;
    }

    ay_print_yang_nesting_begin(ctx);
    ret = ay_get_regex_standardized(rp, buffer);
    AY_CHECK_RET(ret);
    ly_print(ctx->out, "%*spattern \"%s\";\n", ctx->space, "", buffer);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

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

static int
ay_print_yang_leaflist(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sleaf-list ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ret = ay_print_yang_type(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_data_path(ctx, node);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

static int
ay_print_yang_leaf(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ret = ay_print_yang_type(ctx, node);
    AY_CHECK_RET(ret);
    ret = ay_print_yang_default_value(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_data_path(ctx, node);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

static int
ay_print_yang_leaf_key(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct lens *label, *value;

    ly_print(ctx->out, "%*sleaf ", ctx->space, "");

    ay_print_yang_ident(ctx, node);
    ay_print_yang_nesting_begin(ctx);
    label = AY_LABEL_LENS(node);
    value = AY_VALUE_LENS(node);
    if ((label && (label->tag == L_SEQ)) || (!label && !value)) {
        ly_print(ctx->out, "%*stype uint64;\n", ctx->space, "");
    } else {
        ret = ay_print_yang_type(ctx, node);
        AY_CHECK_RET(ret);
        ret = ay_print_yang_default_value(ctx, node);
        AY_CHECK_RET(ret);
    }
    ay_print_yang_data_path(ctx, node);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

static int
ay_print_yang_list_key(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;
    struct ay_ynode *iter;

    ly_print(ctx->out, "%*skey \"", ctx->space, "");
    for (iter = node->child; iter; iter = iter->next) {
        if (iter->type == YN_KEY) {
            ay_print_yang_ident(ctx, iter);
        }
    }
    ly_print(ctx->out, "\";\n");

    return ret;
}

static int
ay_print_yang_list(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*slist ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);

    ret = ay_print_yang_list_key(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_data_path(ctx, node);
    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);

    ay_print_yang_nesting_end(ctx);

    return ret;
}

static int
ay_print_yang_container(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    int ret = 0;

    ly_print(ctx->out, "%*scontainer ", ctx->space, "");
    ret = ay_print_yang_ident(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_begin(ctx);
    ay_print_yang_data_path(ctx, node);
    ret = ay_print_yang_children(ctx, node);
    AY_CHECK_RET(ret);
    ay_print_yang_nesting_end(ctx);

    return ret;
}

static int
ay_print_yang_unknown(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    (void)ctx;
    (void)node;
    return 0;
}

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
        ret = ay_print_yang_leaf_key(ctx, node);
        break;
    case YN_ROOT:
        ret = ay_print_yang_children(ctx, node);
        break;
    }

    return ret;
}

static int
ay_print_yang_case(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    ly_print(ctx->out, "%*scase ", ctx->space, "");
    ay_print_yang_ident(ctx, node);

    return 0;
}

static int
ay_print_yang_choice(struct yprinter_ctx *ctx, struct ay_ynode *node)
{
    if (node->parent) {
        ly_print(ctx->out, "%*schoice ", ctx->space, "");
        ay_print_yang_ident(ctx, node->parent);
    } else {
        ly_print(ctx->out, "%*schoice %s", ctx->space, "", ctx->mod->name);
    }

    return 0;
}

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
        if (iter->snode && (iter->choice == choice)) {
            first = iter == node ? 1 : 0;
            break;
        }
    }

    next_has_same_choice = node->next && (node->next->choice == choice);
    alone = first && !next_has_same_choice;
    last = !first && !next_has_same_choice;

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

    ctx.space = SPACE_INDENT;
    ctx.mod = mod;
    ctx.tree = tree;
    ctx.out = out;

    ly_print(out, "module ", mod->name);
    ay_print_yang_module_name(&ctx);
    ly_print(out, " {\n");
    ly_print(out, "  namespace \"aug:");
    ay_print_yang_module_name(&ctx);
    ly_print(out, "\";\n");
    ly_print(out, "  prefix aug;\n\n");
    ly_print(out, "  import augeas-extension {\n");
    ly_print(out, "    prefix "AY_EXT_PREFIX ";\n");
    ly_print(out, "  }\n\n");

    ret = ay_print_yang_node(&ctx, tree);

    ly_print(out, "}");
    ly_out_free(out, NULL, 0);

    *str_out = str;
    return ret;
}

static void
ay_print_void(struct lprinter_ctx *ctx)
{
    (void)(ctx);
    return;
}

static ly_bool
ay_print_lens_filter_ynode(struct lprinter_ctx *ctx)
{
    struct lens *lens;

    lens = ctx->data;
    return !(lens->tag == L_SUBTREE);
}

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

static void
ay_print_ynode_transition_lv(struct lprinter_ctx *ctx)
{
    ay_print_ynode_label_value(ctx, ctx->data);
    ay_print_ynode_transition(ctx);
}

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
    case YN_ROOT:
        ly_print(ctx->out, "%*s ynode_tag: YN_ROOT\n", ctx->space, "");
        break;
    }

    if (node->choice) {
        ly_print(ctx->out, "%*s choice_id: %p\n", ctx->space, "", node->choice);
    }
}

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

static int
ay_ynode_debug_tree(uint64_t vercode, uint64_t vermask, struct ay_ynode *tree)
{
    int ret = 0;
    char *str1;
    struct lprinter_ctx_f print_func = {0};

    assert(tree->type == YN_ROOT);

    if (!(vercode & vermask)) {
        return 0;
    }

    print_func.transition = ay_print_ynode_transition_lv;
    print_func.extension = ay_print_ynode_extension;
    ret = ay_print_lens(tree, &print_func, NULL, &str1);
    AY_CHECK_RET(ret);

    printf("%s\n", str1);
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

static void
ay_ynode_connect1(struct ay_ynode *forest, struct ay_ynode *parent, struct ay_ynode **child_out)
{
    struct ay_ynode *child, *iter;

    /* memory location assignment */
    child = &forest[LY_ARRAY_COUNT(forest)];
    LY_ARRAY_INCREMENT(forest);

    /* set parent */
    child->parent = parent;

    if (parent && !parent->child) {
        /* set child */
        parent->child = child;
    } else if (parent) {
        /* set next */
        for (iter = parent->child; iter->next; iter = iter->next) {}
        iter->next = child;
    } else if (!parent && (LY_ARRAY_COUNT(forest) > 1)) {
        /* set 'next' for the second and the others root nodes */
        for (uint32_t j = 0; j < LY_ARRAY_COUNT(forest); j++) {
            iter = &forest[j];
            if (!iter->next) {
                iter->next = child;
                break;
            }
            j += iter->descendants;
        }
    }

    *child_out = child;
}

static void
ay_ynode_connect2(struct ay_ynode *parent, struct ay_ynode *child)
{
    if (parent) {
        parent->descendants += 1 + child->descendants;
    }
}

static void
ay_ynode_create_forest_r(struct ay_ynode *yforest, struct ay_lnode *lnode, struct ay_ynode *parent)
{
    struct ay_ynode *child;
    uint32_t iter_start;
    enum lens_tag tag;

    iter_start = parent ? 1 : 0;

    for (uint32_t i = iter_start; i < lnode->descendants; i++) {
        tag = lnode[i].lens->tag;
        if (tag == L_SUBTREE) {
            ay_ynode_connect1(yforest, parent, &child);
            child->snode = &lnode[i];
            ay_ynode_create_forest_r(yforest, &lnode[i], child);
            ay_ynode_connect2(parent, child);
            i += lnode[i].descendants;
        }
    }
}

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

static void
ay_ynode_create_forest(struct ay_lnode *ltree, struct ay_ynode *yforest)
{
    ay_ynode_create_forest_r(yforest, ltree, NULL);
    ay_ynode_add_label_value(yforest);
    ay_ynode_add_choice(yforest);
}

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

static void
ay_ynode_shift_right(struct ay_ynode *dst)
{
    memmove(&dst[1], dst, LY_ARRAY_COUNT(dst) * sizeof *dst);
    LY_ARRAY_INCREMENT(dst);
    memset(dst, 0, sizeof *dst);

    for (uint32_t i = 1; i < LY_ARRAY_COUNT(dst); i++) {
        dst[i].parent = dst[i].parent ? dst[i].parent + 1 : NULL;
        dst[i].next = dst[i].next ? dst[i].next + 1 : NULL;
        dst[i].child = dst[i].child ? dst[i].child + 1 : NULL;
    }
}

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

static ly_bool
ay_ynode_rule_list(struct ay_ynode *node)
{
    return node->child && node->label && ay_ynode_has_repetition(node);
}

static ly_bool
ay_ynode_rule_container(struct ay_ynode *node)
{
    return node->child && node->label;
}

static ly_bool
ay_ynode_rule_leaflist(struct ay_ynode *node)
{
    return !node->child && node->label && ay_ynode_has_repetition(node);
}

static ly_bool
ay_ynode_rule_leaf(struct ay_ynode *node)
{
    return !node->child && node->label;
}

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

static void
ay_ynode_insert_node(struct ay_ynode *dst, uint32_t index)
{
    memmove(&dst[index + 1], &dst[index], (LY_ARRAY_COUNT(dst) - index) * sizeof *dst);
    memset(&dst[index], 0, sizeof *dst);

    LY_ARRAY_INCREMENT(dst);
    for (uint32_t i = 0; i < index; i++) {
        dst[i].next = dst[i].next > &dst[index] ? dst[i].next + 1 : dst[i].next;
    }
    for (uint32_t i = index + 1; i < LY_ARRAY_COUNT(dst); i++) {
        if (dst[i].parent >= &dst[index]) {
            dst[i].parent += 1;
        }
        dst[i].child = dst[i].child ? dst[i].child + 1 : NULL;
        dst[i].next = dst[i].next ? dst[i].next + 1 : NULL;
    }
}

static void
ay_ynode_remove_node_(struct ay_ynode *dst, uint32_t index)
{
    memmove(&dst[index], &dst[index + 1], (LY_ARRAY_COUNT(dst) - index - 1) * sizeof *dst);

    LY_ARRAY_DECREMENT(dst);
    for (uint32_t i = 0; i < index; i++) {
        dst[i].next = dst[i].next > &dst[index] ? dst[i].next - 1 : dst[i].next;
    }
    for (uint32_t i = index; i < LY_ARRAY_COUNT(dst); i++) {
        dst[i].parent = dst[i].parent >= &dst[index] ? dst[i].parent - 1 : dst[i].parent;
        dst[i].child = dst[i].child ? dst[i].child - 1 : NULL;
        dst[i].next = dst[i].next ? dst[i].next - 1 : NULL;
    }
}

static void
ay_ynode_remove_node(struct ay_ynode *dst, uint32_t index)
{
    struct ay_ynode *iter, *removed, *last = NULL;

    removed = &dst[index];

    /* the number of descendants is reduced by one */
    for (iter = removed->parent; iter; iter = iter->parent) {
        iter->descendants--;
    }

    /* for all children */
    for (iter = removed->child; iter; iter = iter->next) {
        /* set new parent for children */
        iter->parent = removed->parent;
        /* remember last child */
        last = iter;
    }
    if (last) {
        /* set last child's sibling */
        last->next = removed->next;
    }

    if (removed->parent && !removed->parent->descendants) {
        /* removed node is an only child */
        removed->parent->child = NULL;
    }

    if (!removed->next && removed->parent && !removed->child) {
        /* set 'next' for the previous sibling to NULL */
        for (iter = removed->parent->child; iter; iter = iter->next) {
            iter->next = iter->next == removed ? NULL : iter->next;
        }
    }

    ay_ynode_remove_node_(dst, index);
}

static void
ay_ynode_insert_wrapper(struct ay_ynode *dst, uint32_t index)
{
    struct ay_ynode *iter, *wrapper, *child;

    wrapper = &dst[index];
    child = &dst[index + 1];
    ay_ynode_insert_node(dst, index);

    wrapper->parent = child->parent;
    wrapper->next = child->next;
    wrapper->child = child;
    wrapper->descendants = child->descendants + 1;
    wrapper->snode = NULL;
    wrapper->type = YN_UNKNOWN;

    child->parent = wrapper;
    child->next = NULL;

    for (iter = wrapper->parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
}

static void
ay_ynode_insert_parent(struct ay_ynode *dst, uint32_t index)
{
    struct ay_ynode *parent, *wrapper;

    assert(dst->type == YN_ROOT);

    wrapper = dst[index].parent;
    ay_ynode_insert_wrapper(dst, AY_INDEX(dst, wrapper));
    parent = wrapper->child;

    wrapper->type = parent->type;
    wrapper->snode = parent->snode;
    wrapper->label = parent->label;
    wrapper->value = parent->value;
    wrapper->choice = parent->choice;
    parent->type = YN_UNKNOWN;
    parent->snode = NULL;
    parent->label = NULL;
    parent->value = NULL;
    parent->choice = NULL;
}

static void
ay_ynode_insert_child(struct ay_ynode *dst, uint32_t index)
{
    struct ay_ynode *iter, *parent, *new_child;

    parent = &dst[index];
    new_child = &dst[index + 1];
    ay_ynode_insert_node(dst, index + 1);

    new_child->parent = parent;
    new_child->next = parent->child ? &dst[index + 2] : NULL;
    new_child->child = NULL;
    new_child->descendants = 0;
    new_child->snode = NULL;
    new_child->type = YN_UNKNOWN;
    new_child->label = NULL;
    new_child->choice = NULL;

    parent->child = new_child;

    for (iter = parent; iter; iter = iter->parent) {
        iter->descendants++;
    }
}

static int
ay_ynode_debug_insert_remove(uint64_t vercode, struct ay_ynode *forest)
{
    int ret = 0;
    const char *msg;
    struct ay_ynode *dupl = NULL, *snap = NULL, *tree = NULL;

    if (!vercode) {
        return ret;
    }

    LY_ARRAY_CREATE_GOTO(NULL, dupl, LY_ARRAY_COUNT(forest) + 1, ret, end);
    LY_ARRAY_CREATE_GOTO(NULL, snap, LY_ARRAY_COUNT(forest), ret, end);

    msg = "ynode insert_child";
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(forest); i++) {
        ay_ynode_copy(dupl, forest);
        memcpy(snap, dupl, LY_ARRAY_COUNT(forest) * sizeof *forest);
        ay_ynode_insert_child(dupl, i);
        ay_ynode_remove_node(dupl, i + 1);
        AY_CHECK_GOTO(memcmp(snap, dupl, LY_ARRAY_COUNT(forest) * sizeof *forest), error);
        AY_SET_LY_ARRAY_SIZE(dupl, 0);
    }

    LY_ARRAY_FREE(dupl);
    LY_ARRAY_FREE(snap);
    dupl = snap = NULL;
    ret = ay_ynode_create_tree(forest, &tree);
    AY_CHECK_GOTO(ret, end);
    LY_ARRAY_CREATE_GOTO(NULL, dupl, LY_ARRAY_COUNT(tree) + 1, ret, end);
    LY_ARRAY_CREATE_GOTO(NULL, snap, LY_ARRAY_COUNT(tree), ret, end);

    msg = "ynode insert_parent";
    for (uint32_t i = 1; i < LY_ARRAY_COUNT(tree); i++) {
        ay_ynode_copy(dupl, tree);
        memcpy(snap, dupl, LY_ARRAY_COUNT(tree) * sizeof *tree);
        ay_ynode_insert_parent(dupl, i);
        ay_ynode_remove_node(dupl, AY_INDEX(dupl, dupl[i + 1].parent));
        AY_CHECK_GOTO(memcmp(snap, dupl, LY_ARRAY_COUNT(forest) * sizeof *forest), error);
        AY_SET_LY_ARRAY_SIZE(dupl, 0);
    }

end:
    if (ret) {
        printf(AY_NAME " DEBUG: %s failed\n", msg);
    }
    LY_ARRAY_FREE(dupl);
    LY_ARRAY_FREE(snap);
    LY_ARRAY_FREE(tree);

    return ret;

error:
    ret = AYE_DEBUG_FAILED;
    goto end;
}

static void
ay_remove_type_unknown(struct ay_ynode *dst)
{
    for (uint32_t i = 0; i < LY_ARRAY_COUNT(dst); i++) {
        if ((dst[i].type == YN_UNKNOWN) && (!dst[i].child)) {
            ay_ynode_remove_node(dst, i);
            i--;
        }
    }
}

static void
ay_remove_comment(struct ay_ynode *dst)
{
    struct ay_ynode *iter;
    struct lens *label;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(dst); i++) {
        iter = &dst[i];
        label = AY_LABEL_LENS(iter);
        if (label && (label->tag == L_LABEL)) {
            if (!strcmp("#comment", label->string->str)) {
                ay_ynode_remove_node(dst, i);
                i--;
            }
        }
    }
}

static void
ay_remove_useless_leaf_r(struct ay_ynode *tree, uint32_t index)
{
    struct ay_ynode *parent, *iter1, *iter2;
    struct lens *wanted;
    uint32_t child_idx;

    parent = &tree[index];
    if (!parent->child) {
        return;
    }

    for (uint32_t i = 0; i < parent->descendants; i++) {
        child_idx = index + 1 + i;
        iter1 = &tree[child_idx];
        ay_remove_useless_leaf_r(tree, child_idx);
        if (iter1->snode && (iter1->type == YN_LEAF)) {
            wanted = iter1->snode->lens;
            for (iter2 = parent->child; iter2; iter2 = iter2->next) {
                if (iter2->snode && (iter2->type == YN_LEAFLIST) && (iter2->snode->lens == wanted)) {
                    ay_ynode_remove_node(tree, AY_INDEX(tree, iter1));
                    i--;
                    break;
                }
            }
        }
    }
}

static void
ay_remove_useless_leaf(struct ay_ynode *tree)
{
    ay_remove_useless_leaf_r(tree, 0);
}

static void
ay_remove_top_choice(struct ay_ynode *tree)
{
    struct ay_ynode *iter;
    struct ay_lnode *choice;

    if ((tree->child->type == YN_LIST) && tree->child->choice) {
        /* all lists must have the same choice */
        choice = tree->child->choice;
    } else {
        return;
    }

    /* check if all nodes are lists */
    for (iter = tree->child; iter; iter = iter->next) {
        if ((iter->type != YN_LIST) || (choice != iter->choice)) {
            return;
        }
    }

    /* remove choice */
    for (iter = tree->child; iter; iter = iter->next) {
        iter->choice = NULL;
    }
}

static void
ay_insert_data_container(struct ay_ynode *dst)
{
    ay_ynode_insert_parent(dst, 1);
    dst[1].type = YN_CONTAINER;
}

static void
ay_insert_list_key(struct ay_ynode *dst)
{
    struct ay_ynode *parent;
    struct lens *label, *value;
    uint32_t parent_idx;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(dst); i++) {
        parent = &dst[i];
        parent_idx = i;
        if (!ay_ynode_rule_list_key(parent)) {
            continue;
        }
        label = AY_LABEL_LENS(parent);
        value = AY_VALUE_LENS(parent);

        if (!value && label && (label->tag == L_LABEL)) {
            ay_ynode_insert_child(dst, parent_idx);
            dst[parent_idx + 1].type = YN_KEY;
            i++;
            continue;
        } else if (value && (value->tag == L_STORE) && label && (label->tag == L_LABEL)) {
            ay_ynode_insert_child(dst, parent_idx);
            dst[parent_idx + 1].type = YN_KEY;
            dst[parent_idx + 1].value = parent->value;
            i++;
            continue;
        } else if (value && (value->tag == L_STORE)) {
            ay_ynode_insert_child(dst, parent_idx);
            dst[parent_idx + 1].type = YN_LEAF;
            dst[parent_idx + 1].value = parent->value;
            i++;
        }

        if (label) {
            ay_ynode_insert_child(dst, parent_idx);
            dst[parent_idx + 1].type = YN_KEY;
            dst[parent_idx + 1].label = parent->label;
            i++;
        }
    }
}

static void
ay_ynode_set_type(struct ay_ynode *dst)
{
    struct ay_ynode *node;

    for (uint32_t i = 0; i < LY_ARRAY_COUNT(dst); i++) {
        node = &dst[i];
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

static int
ay_ynode_transformations(uint64_t vercode, struct ay_ynode **tree)
{
    int ret = 0;

    assert((*tree)->type == YN_ROOT);

    /* set type */
    ay_ynode_set_type(*tree);

    /* remove unnecessary nodes */
    ay_remove_type_unknown(*tree);
    ay_remove_comment(*tree);
    ay_remove_useless_leaf(*tree);
    ay_remove_top_choice(*tree);

    ret = ay_ynode_debug_tree(vercode, AYV_TRANS_REMOVE, *tree);
    AY_CHECK_RET(ret);

    ay_ynode_trans_insert2(tree, 1, ay_insert_data_container);

    ay_ynode_trans_insert1(tree, ay_ynode_rule_list_key, ay_insert_list_key, 2);
    ret = ay_ynode_debug_tree(vercode, AYV_TRANS_LIST_KEY, *tree);
    AY_CHECK_RET(ret);

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

    LY_ARRAY_CREATE_GOTO(NULL, ltree, ltree_size, ret, end);
    ay_lnode_create_tree(ltree, lens, ltree);
    ay_lnode_debug_tree(vercode, mod, ltree);

    LY_ARRAY_CREATE_GOTO(NULL, yforest, yforest_size, ret, end);
    ay_ynode_create_forest(ltree, yforest);
    ay_ynode_debug_forest(vercode, mod, yforest);

    AY_CHECK_GOTO(ay_ynode_debug_copy(vercode, yforest), end);
    AY_CHECK_GOTO(ay_ynode_debug_insert_remove(vercode, yforest), end);

    ret = ay_ynode_create_tree(yforest, &ytree);
    AY_CHECK_GOTO(ret, end);
    ret = ay_ynode_debug_tree(vercode, AYV_YTREE, ytree);
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
