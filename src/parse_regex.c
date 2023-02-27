/**
 * @file parse_regex.c
 * @author Adam Piecek <piecek@cesnet.cz>
 * @brief Functions for parsing regexes.
 *
 * @copyright
 * Copyright (c) 2023 Deutsche Telekom AG.
 * Copyright (c) 2023 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 *
 * @section AY_PARSE_REGEX Regex parsing
 *
 * Augyang in some cases needs to parse a regular expression that is entered in Augeas lense by the command 'key'
 * or 'store'. From the expression, the Augyang deduces which nodes need to be added to the Yang file. For example,
 * from the expression 'key "a" | "b"' deduces that the node named 'a' and node named 'b' need to be added.
 * Additionally, name derivation becomes even more complicated if '?' is also included. For example, from expression
 * 'key /values?/' deduces nodes 'value' and 'values'. The minus operator is also problematic. Overall, it would be
 * much more reliable if some library could be found that could reliably create such name derivations.
 *
 */

#define _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include <libyang/libyang.h>

#include "augyang.h"
#include "common.h"
#include "lens.h"
#include "parse_regex.h"

/**
 * @brief Check if @p str is basically a size-insensitive character.
 *
 * @param[in] str String or pattern to check.
 * @return 1 if @p str is the size-insensitive character.
 */
static ly_bool
ay_ident_character_nocase(const char *str)
{
    char ch1, ch2;

    /* match pattern like [Aa] */
    if ((str[0] == '[') && ((ch1 = str[1])) && isupper(ch1) && ((ch2 = str[2])) && islower(ch2) &&
            (ch1 == toupper(ch2)) && (str[3] == ']')) {
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if string @p str is equal to the allowed pattern.
 *
 * Typically, it is a regular expression related to spaces.
 *
 * @param[in] str String (identifier) to check.
 * @param[out] shift Length of the found pattern - 1.
 * @return 1 if pattern in identifier is valid.
 */
static ly_bool
ay_ident_pattern_is_valid(const char *str, uint32_t *shift)
{
    *shift = 0;

    if (!strncmp(str, "[ ]+", 4) ||
            /* match pattern like [Aa] */
            ay_ident_character_nocase(str)) {
        *shift = 3;
        return 1;
    } else {
        return 0;
    }
}

/**
 * @brief Check if character is valid as part of identifier.
 *
 * @param[in] ch Character to check.
 * @param[out] shift Number is set to 1 if the next character should not be read in the next iteration
 * because it is preceded by a backslash. Otherwise is set to 0.
 * @return 1 for valid character, otherwise 0.
 */
static ly_bool
ay_ident_character_is_valid(const char *ch, uint32_t *shift)
{
    assert(ch && shift);

    *shift = 0;

    if (((*ch >= 65) && (*ch <= 90)) || /* A-Z */
            ((*ch >= 97) && (*ch <= 122)) || /* a-z */
            ((*ch >= 48) && (*ch <= 57))) { /* 0-9 */
        return 1;
    } else if (((*ch == '\\') && (*(ch + 1) == '.')) ||
            ((*ch == '\\') && (*(ch + 1) == '-')) ||
            ((*ch == '\\') && (*(ch + 1) == '+'))) {
        *shift = 1;
        return 1;
    } else {
        switch (*ch) {
        case ' ':
        case '-':
        case '_':
            return 1;
        default:
            return 0;
        }
    }
}

struct ay_transl *
ay_lense_pattern_has_idents(const struct ay_ynode *tree, const struct lens *lens)
{
    const char *iter, *patt;
    uint32_t shift;

    if (!lens || (lens->tag != L_KEY)) {
        return NULL;
    }

    patt = lens->regexp->pattern->str;

    if (tree) {
        return ay_transl_find(AY_YNODE_ROOT_PATT_TABLE(tree), patt);
    }

    for (iter = patt; *iter != '\0'; iter++) {
        switch (*iter) {
        case '#':
        case '(':
        case '?':
            break;
        case ')':
            if (*(iter + 1) == '?') {
                iter++;
            }
            break;
        case '|':
        case '\n': /* '\n'-> TODO pattern is probably written wrong -> bugfix lense? */
            continue;
        default:
            if (ay_ident_character_is_valid(iter, &shift) || ay_ident_pattern_is_valid(iter, &shift)) {
                iter = shift ? iter + shift : iter;
            } else {
                return NULL;
            }
        }
    }

    /* Success - return some non-NULL address. */
    return (struct ay_transl *)patt;
}

/**
 * @brief Check if all bits in the bitset are set to 0.
 *
 * @param[in] bitset Bits to check.
 * @param[in] size Number of bits in @p bitset.
 * @return 1 if all bits are 0.
 */
static ly_bool
ay_bitset_is_zero(uint8_t *bitset, uint64_t size)
{
    uint64_t i;

    for (i = 0; i < size; i++) {
        if (bitset[i]) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief Find the position of the most significant bit.
 *
 * @param[in] bitset Array of bits.
 * @param[in] size Number of bits in @p bitset.
 * @return Position of most significant bit, or 0 if no bits are set in @p bitset.
 */
static uint64_t
ay_bitset_msb(uint8_t *bitset, uint64_t size)
{
    uint64_t i;

    for (i = size; i && !bitset[i - 1]; i--) {}
    return i ? i : 0;
}

/**
 * @brief Set all bits in @p bitset to zero.
 *
 * @param[in] bitset Array of bits.
 * @param[in] size Number of bits in @p bitset.
 */
static void
ay_bitset_clear(uint8_t *bitset, uint64_t size)
{
    uint64_t i;

    for (i = 0; i < size; i++) {
        bitset[i] = 0;
    }
}

/**
 * @brief Set the most significant bit to zero.
 *
 * @param[in] bitset Array of bits.
 * @param[in] size Number of bits in @p bitset.
 */
static void
ay_bitset_clear_msb(uint8_t *bitset, uint64_t size)
{
    uint64_t msb;

    msb = ay_bitset_msb(bitset, size);
    assert(msb);
    bitset[msb - 1] = 0;
}

/**
 * @brief Convert the @p number to the corresponding sequence of bits.
 *
 * @param[in] bitset Array of bits.
 * @param[in] size Number of bits in @p bitset.
 * @param[in] number Number used to initialize the bitset.
 */
static void
ay_bitset_create_from_uint64(uint8_t *bitset, uint64_t size, uint64_t number)
{
    uint8_t i;

    assert(size <= 63);
    for (i = 0; (i < size); i++) {
        if (number & (1 << i)) {
            bitset[i] = 1;
        } else {
            bitset[i] = 0;
        }
    }
    for ( ; (i < size); i++) {
        bitset[i] = 0;
    }
}

/**
 * @brief Remove character from string.
 *
 * @param[in,out] str Pointer to string.
 * @param[in] rem Pointer to character in @p str to be removed.
 */
static void
ay_string_remove_character(char *str, const char *rem)
{
    uint64_t idx, len;

    assert(str && rem && (rem >= str));
    len = strlen(str);
    idx = rem - str;
    assert(idx < len);
    memmove(&str[idx], &str[idx + 1], len - idx);
}

/**
 * @brief Count the total number of variants that can be derived from the \"[Aa][Bb]...\" pattern.
 *
 * @param[in] ptoken Subpattern to process.
 * @param[in] ptoken_len Length of @p ptoken.
 * @return Number of nocase variations.
 */
static uint64_t
ay_pattern_identifier_nocase_variations(const char *ptoken, uint64_t ptoken_len)
{
    uint64_t i;

    /* Skip '(' characters. */
    for (i = 0; (i < ptoken_len) && (ptoken[i] == '('); i++) {}
    if (!(i < ptoken_len)) {
        return 0;
    }
    ptoken += i;
    ptoken_len -= i;

    if ((ptoken_len >= 4) && ay_ident_character_nocase(ptoken)) {
        return 2;
    } else {
        return 0;
    }
}

/**
 * @brief Count the number of question marks.
 *
 * @param[in] ptoken Subpattern to process.
 * @param[in] ptoken_len Length of @p ptoken.
 * @return Number of question marks.
 */
static uint64_t
ay_pattern_identifier_qm_count(const char *ptoken, uint64_t ptoken_len)
{
    uint64_t i, qm;

    qm = 0;
    for (i = 0; i < ptoken_len; i++) {
        if (ptoken[i] == '?') {
            qm++;
        }
    }

    return qm;
}

/**
 * @brief Count the number of variations from the pattern based on the question mark symbol.
 *
 * @param[in] ptoken Subpattern to process.
 * @param[in] ptoken_len Length of @p ptoken.
 * @return Number of variations.
 */
static uint64_t
ay_pattern_identifier_qm_variations(const char *ptoken, uint64_t ptoken_len)
{
    uint64_t i, qm, ret;

    qm = ay_pattern_identifier_qm_count(ptoken, ptoken_len);

    ret = 1;
    for (i = 0; i < qm; i++) {
        ret *= 2;
    }

    return ret;
}

/**
 * @brief Count number of identifiers in the lense pattern.
 *
 * @param[in] patt Lense pattern to check.
 * @return Number of identifiers.
 */
static uint64_t
ay_pattern_idents_count(const char *patt)
{
    uint64_t ret, len;
    const char *vbar, *prev_vbar;

    ret = 0;
    vbar = prev_vbar = patt;
    while ((vbar = strchr(vbar, '|'))) {
        vbar++;
        assert(*vbar);
        len = vbar - prev_vbar;
        ret += ay_pattern_identifier_nocase_variations(prev_vbar, len);
        ret += ay_pattern_identifier_qm_variations(prev_vbar, len);
        prev_vbar = vbar;
    }
    ret += ay_pattern_identifier_qm_variations(prev_vbar, strlen(prev_vbar));

    return ret;
}

/**
 * @brief Get main union token from pattern (lens.regexp.pattern.str).
 *
 * Pattern must be for example in form: name1 | name2 | (pref1|pref2)name3 | name4(post1|post2)
 * Then the tokens are: name1, name2, (pref1|pref2)name3, name4(post1|post2)
 * If pattern is for example in form: name1 | name2) | name3 | name4
 * Then the tokens are: name1, name2
 * (Tokens name3 and name4 are not accessible by index)
 *
 * @param[in] patt Pattern string from lens.regexp.pattern.str.
 * @param[in] idx Index of the requested token.
 * @param[out] token_len Token length. It ends at '|' or ')'.
 * @return Pointer to token in @p patt on index @p idx or NULL.
 */
static const char *
ay_pattern_union_token(const char *patt, uint64_t idx, uint64_t *token_len)
{
    uint64_t par, cnt;
    const char *ret, *iter, *start, *stop;
    ly_bool end;

    assert(patt && token_len);

    if (*patt == '\0') {
        return NULL;
    } else if (*patt == '|') {
        patt++;
    }

    start = patt;
    stop = NULL;
    par = 0;
    cnt = 0;
    end = 0;
    for (iter = patt; *iter && !end; iter++) {
        switch (*iter) {
        case '(':
            par++;
            break;
        case ')':
            if (!par) {
                /* Interpret as the end of input. */
                stop = iter;
                end = 1;
            } else {
                par--;
            }
            break;
        case '|':
            if (par) {
                break;
            } else if (cnt == idx) {
                /* Token on index 'idx' has been read. */
                stop = iter;
                end = 1;
            } else if ((cnt + 1) == idx) {
                /* The beginning of the token is found. */
                start = iter + 1;
                cnt++;
            } else {
                cnt++;
            }
            break;
        }
    }
    assert(*start != '\0');

    if (cnt != idx) {
        /* Token not found. */
        *token_len = 0;
        return NULL;
    }
    if (!stop) {
        assert(*iter == '\0');
        stop = iter;
    }
    assert(stop > start);
    *token_len = (uint64_t)(stop - start);

    if ((start == patt) && (idx != 0)) {
        /* Token not found. */
        ret = NULL;
    } else {
        ret = start;
    }

    return ret;
}

/**
 * @brief Duplicate pattern and remove all unnecessary parentheses.
 *
 * Examples of unnecessary parentheses: (abc) -> abc, (abc)|(efg) -> abc|efg, ((abc)|(efg))|hij -> abc|efg|hij
 *
 * @param[in] patt Pattern string from lens.regexp.pattern.str.
 * @return New allocated pattern string without parentheses or NULL in case of memory error.
 */
static char *
ay_pattern_remove_parentheses(const char *patt)
{
    uint64_t i, len, par_removed, par;
    char *buf, *buffer;
    const char *ptoken;

    buffer = strdup(patt);
    if (!buffer) {
        return NULL;
    }
    buf = buffer;

    while ((ptoken = ay_pattern_union_token(buf, 0, &len))) {
        par_removed = 0;
        if ((ptoken[0] == '(') && (ptoken[len - 1] == ')')) {
            par = 1;
            for (i = 1; (i < len) && (par != 0); i++) {
                if (ptoken[i] == '(') {
                    par++;
                } else if (ptoken[i] == ')') {
                    par--;
                }
            }
            if (i == len) {
                /* remove parentheses */
                ay_string_remove_character(buf, &ptoken[len - 1]);
                ay_string_remove_character(buf, ptoken);
                len -= 2;
                par_removed = 1;
            }
        }
        if (!par_removed) {
            /* Shift to the next token. */
            buf = (char *)(ptoken + len);
        }
        /* Else try remove parentheses again. */
    }

    return buffer;
}

/**
 * @brief Check if the union token can be processed.
 *
 * @param[in] ptoken Subpattern to check.
 * @param[in] ptoken_len Length of @p ptoken.
 * @return 1 if union token can be processed.
 */
static ly_bool
ay_pattern_union_token_is_valid(const char *ptoken, uint64_t ptoken_len)
{
    uint64_t i, qm, vbar, opbr;

    /* Check for nocase pattern - ([Aa][Bb]...). */
    if (ay_ident_character_nocase(ptoken)) {
        for (i = 4; i < ptoken_len; i += 4) {
            if (!ay_ident_character_nocase(&ptoken[i])) {
                return 0;
            }
        }
        return 1;
    }

    qm = vbar = opbr = 0;
    for (i = 0; i < ptoken_len; i++) {
        switch (ptoken[i]) {
        case '(':
            opbr++;
            break;
        case ')':
            if (ptoken[i + 1] == '?') {
                qm++;
            }
            break;
        case '|':
            vbar++;
            break;
        }
    }

    if ((qm && (qm != opbr)) ||
            /* There is no algorithm implemented that can process several '?' and '|' in @p ptoken. */
            (qm && vbar && (qm != 1))) {
        return 0;
    }

    return 1;
}

/**
 * @brief Get identifier from union token located in pattern based on vertical bar ('|').
 *
 * @p ptoken must be in form:
 * a) (variation1 | variation2 | ... ) postfix,
 * b) prefix (variation1 | variation2 | ...)
 * c) prefix (variation1 | variation2) postfix
 * d) some_string
 *
 * @param[in] ptoken Union pattern token. See ay_pattern_union_token().
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] idx Index to the requested identifier.
 * @param[out] buffer Buffer of sufficient size in which the identifier will be written.
 * @return AYE_IDENT_NOT_FOUND, AYE_IDENT_LIMIT or 0 on success.
 */
static int
ay_pattern_identifier_vbar_(const char *ptoken, uint64_t ptoken_len, uint64_t idx, char *buffer)
{
    const char *start, *stop, *prefix_end, *postfix_start, *variation;
    uint64_t prefix_len, vari_len, postfix_len;

    stop = ptoken + ptoken_len;
    assert((*stop == '\0') || (*stop == '|'));

    start = ptoken;
    buffer[0] = '\0';
    prefix_end = (const char *)memchr(start, '(', stop - start);
    /* Find ')'. */
    postfix_start = (const char *)memchr(start, ')', stop - start);
    /* Skip '?'. */
    postfix_start = postfix_start && (*(postfix_start + 1) == '?') ? postfix_start + 1 : postfix_start;
    /* Check if postfix exists. */
    postfix_start = postfix_start && (postfix_start + 1 < stop) ? postfix_start + 1 : NULL;

    /* No prefix and no suffix. */
    if (!prefix_end && !postfix_start) {
        if (idx == 0) {
            /* Copy whole string. */
            AY_CHECK_COND(ptoken_len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            strncat(buffer, ptoken, ptoken_len);
            return 0;
        } else {
            /* No other variation. */
            return AYE_IDENT_NOT_FOUND;
        }
    }

    /* Copy string before (variation1 | variation2) pattern. */
    if (prefix_end) {
        prefix_len = prefix_end - start;
        AY_CHECK_COND(prefix_len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        strncat(buffer, start, prefix_len);
    } else {
        /* string before variation is empty */
        prefix_len = 0;
        prefix_end = start;
    }

    /* Choose variation by @p idx. */
    variation = ay_pattern_union_token(prefix_end + 1, idx, &vari_len);
    if (!variation) {
        buffer[0] = '\0';
        return AYE_IDENT_NOT_FOUND;
    }
    AY_CHECK_COND(prefix_len + vari_len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
    strncat(buffer, variation, vari_len);

    /* Copy string after (variation1 | variation2) pattern. */
    if (postfix_start) {
        postfix_len = stop - postfix_start;
        AY_CHECK_COND(prefix_len + vari_len + postfix_len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        strncat(buffer, postfix_start, postfix_len);
    } /* else postfix string is empty */

    return 0;
}

/**
 * @brief Special allowed patterns are deleted in the @p substr.
 *
 * @param[in,out] substr String to be converted.
 */
static void
ay_trans_substr_conversion(char *substr)
{
    uint64_t i, j, len;
    uint32_t shift;

    if (!substr) {
        return;
    }

    len = strlen(substr);
    for (i = 0; i < len; i++) {
        if (ay_ident_pattern_is_valid(&substr[i], &shift)) {
            /* Remove subpattern and replaced it with ' '. */
            for (j = 0; j < shift; j++) {
                ay_string_remove_character(substr, &substr[i]);
            }
            substr[i] = ' ';
        }
    }
}

/**
 * @brief Add identifier to record in translation table.
 *
 * @param[in,out] tran Corresponding record from translation table.
 * @param[in] buffer Buffer containing the identifier.
 * @return 0 on success.
 */
static int
ay_pattern_identifier_add(struct ay_transl *tran, char *buffer)
{
    char *ident;

    if (buffer[0] == '\0') {
        return 0;
    }

    ident = strdup(buffer);
    if (!ident) {
        return AYE_MEMORY;
    }
    ay_trans_substr_conversion(ident);
    tran->substr[LY_ARRAY_COUNT(tran->substr)] = ident;
    LY_ARRAY_INCREMENT(tran->substr);

    return 0;
}

/**
 * @brief Store all identifiers from union token located in pattern based on vertical bar ('|').
 *
 * @param[in] ptoken Union token.
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] buffer Buffer for temporary storage of the identifier.
 * @param[in,out] tran Record from translation table in which the identifiers are stored.
 * @return 0 on success.
 */
static int
ay_pattern_identifier_vbar(const char *ptoken, uint64_t ptoken_len, char *buffer, struct ay_transl *tran)
{
    int ret = 0;
    uint64_t i;

    for (i = 0; !(ret = ay_pattern_identifier_vbar_(ptoken, ptoken_len, i, buffer)); i++) {
        AY_CHECK_COND(ret == AYE_IDENT_LIMIT, ret);
        ret = ay_pattern_identifier_add(tran, buffer);
        AY_CHECK_RET(ret);
    }

    ret = ret == AYE_IDENT_NOT_FOUND ? 0 : ret;
    return ret;
}

/**
 * @brief Add size-insensitive identifiers.
 *
 * @param[in] ptoken Subpattern to process.
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] buffer Buffer for temporary storage of the identifier.
 * @param[in,out] tran Record from translation table in which the identifiers are stored.
 */
static int
ay_pattern_identifier_nocase(const char *ptoken, uint64_t ptoken_len, char *buffer, struct ay_transl *tran)
{
    int ret;
    uint64_t i, j;

    /* Add word in upper-case.
     * [Aa][Ll]...
     *  ^   ^  ... */
    for (i = 1, j = 0; i < ptoken_len; i += 4, j++) {
        buffer[j] = ptoken[i];
    }
    buffer[j] = '\0';
    ret = ay_pattern_identifier_add(tran, buffer);
    AY_CHECK_RET(ret);

    /* Add word in lower-case.
     * [Aa][Ll]...
     *   ^   ^ ... */
    for (i = 2, j = 0; i < ptoken_len; i += 4, j++) {
        buffer[j] = ptoken[i];
    }
    buffer[j] = '\0';

    ret = ay_pattern_identifier_add(tran, buffer);
    AY_CHECK_RET(ret);

    return 0;
}

/**
 * @brief Get identifier from union token located in pattern based on question mark ('?').
 *
 * Example:
 * ptoken is "ab(cd(ef)?)?".
 * - theoretical total number of variations is 4, so total_vari is 4.
 * variation | vari | buffer
 * 1         | 00   | "ab"
 * 2         | 01   | "abcd"
 * 3         | 10   | "" (invalid variation, due to dependency between question marks)
 * 4         | 11   | "abcdef"
 *
 * Question mark without parenthesis (etc. abc?) is not supported.
 *
 * @param[in] ptoken Union token.
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] flag Bitset for temporary storage of flag. It is used for the @p ptoken analysis.
 * @param[in] vari Bitset of desired variation.
 * @param[in] total_vari Length of @p flag and @p vari.
 * @param[out] buffer Buffer in which the identifier is stored.
 * @return 0 on success.
 */
static int
ay_pattern_identifier_qm_(const char *ptoken, uint64_t ptoken_len, uint8_t *flag, uint8_t *vari, uint64_t total_vari,
        char *buffer)
{
    uint64_t i, j, group, bufidx, msb;

    group = 0;
    bufidx = 0;
    for (i = 0; i < ptoken_len; i++) {
        if (ptoken[i] == '(') {
            flag[group] = 1;
            group++;
            /* The ay_pattern_remove_parentheses() should guarantee that there are no unnecessary parentheses. */
            assert(group < total_vari);
            continue;
        } else if ((ptoken[i] == ')') && (ptoken[i + 1] == '?')) {
            ay_bitset_clear_msb(flag, total_vari);
            i++;
            continue;
        } else if (ay_bitset_is_zero(flag, total_vari) && (ptoken[i + 1] != '?')) {
            AY_CHECK_COND(bufidx >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
            buffer[bufidx++] = ptoken[i];
            continue;
        } else if ((ptoken[i + 1] == '?') && !vari[group]) {
            group++;
            assert(group < total_vari);
            i++;
            continue;
        } else if (((msb = ay_bitset_msb(flag, total_vari))) && !vari[msb - 1]) {
            continue;
        }

        if ((ptoken[i + 1] == '?') && vari[group]) {
            flag[group] = 1;
            msb = group;
        } else {
            msb = ay_bitset_msb(flag, total_vari);
        }

        for (j = 0; j < msb; j++) {
            if (flag[j] && !vari[j]) {
                /* This variation is invalid. The question marks
                 * are nested within each other and one is dependent on the other.
                 */
                buffer[0] = '\0';
                return 0;
            }
        }

        AY_CHECK_COND(bufidx >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        buffer[bufidx++] = ptoken[i];

        if ((ptoken[i + 1] == '?')) {
            flag[group] = 0;
            group++;
            i++;
        }
    }

    buffer[bufidx] = '\0';

    return 0;
}

/**
 * @brief Store all identifiers from union token located in pattern based on question mark ('?').
 *
 * @param[in] ptoken Union token.
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] buffer Buffer for temporary storage of the identifier.
 * @param[in,out] tran Record from translation table in which the identifiers are stored.
 * @return 0 on success.
 */
static int
ay_pattern_identifier_qm(const char *ptoken, uint64_t ptoken_len, char *buffer, struct ay_transl *tran)
{
    int ret = 0;
    uint64_t i, total_vari;
    uint8_t *flag = NULL, *vari = NULL;

    total_vari = ay_pattern_identifier_qm_variations(ptoken, ptoken_len);
    assert(total_vari);
    if (total_vari > 63) {
        return AYE_INTERNAL_ERROR;
    }
    flag = malloc(sizeof(uint8_t) * total_vari);
    vari = malloc(sizeof(uint8_t) * total_vari);
    if (!flag || !vari) {
        ret = AYE_MEMORY;
        goto free;
    }

    for (i = 0; i < total_vari; i++) {
        ay_bitset_clear(flag, total_vari);
        ay_bitset_create_from_uint64(vari, total_vari, i);
        ret = ay_pattern_identifier_qm_(ptoken, ptoken_len, flag, vari, total_vari, buffer);
        AY_CHECK_GOTO(ret, free);
        ret = ay_pattern_identifier_add(tran, buffer);
        AY_CHECK_GOTO(ret, free);
    }

free:
    free(flag);
    free(vari);

    return ret;
}

/**
 * @brief Store all identifiers from union token which contains a subpattern with one question mark and vertical bars.
 *
 * @p ptoken must be in form:
 * a) some_name (postfix1 | postfix2 | ...)?
 * b) (prefix1 | prefix2 | ... )? some_name
 *
 * @param[in] ptoken Union token.
 * @param[in] ptoken_len Length of @p ptoken.
 * @param[in] buffer Buffer for temporary storage of the identifier.
 * @param[in,out] tran Record from translation table in which the identifiers are stored.
 * @return 0 on success.
 */
static int
ay_pattern_identifier_vbar_qm(const char *ptoken, uint64_t ptoken_len, char *buffer, struct ay_transl *tran)
{
    int ret;
    uint64_t len;
    ly_bool qm_postfix;
    char *qm, *par;

    qm = memchr(ptoken, '?', ptoken_len);
    assert(qm);
    qm_postfix = (ptoken + ptoken_len) == (qm + 1);

    if (qm_postfix) {
        /* Expecting: some_name (postfix1 | postfix2 | ...)? */
        ay_pattern_identifier_vbar(ptoken, ptoken_len, buffer, tran);
        /* Apply '?' subpattern. */
        par = memchr(ptoken, '(', ptoken_len);
        len = par - ptoken;
        AY_CHECK_COND(len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        strncat(buffer, ptoken, len);
    } else {
        /* Expecting: (prefix1 | prefix2 | ... )? some_name */
        ay_pattern_identifier_vbar(ptoken, ptoken_len, buffer, tran);
        /* Apply '?' subpattern. */
        len = ptoken_len - ((qm + 1) - ptoken);
        AY_CHECK_COND(len >= AY_MAX_IDENT_SIZE, AYE_IDENT_LIMIT);
        strncat(buffer, qm + 1, len);
    }
    ret = ay_pattern_identifier_add(tran, buffer);

    return ret;
}

int
ay_transl_create_substr(struct ay_transl *tran)
{
    int ret;
    uint64_t cnt, len;
    const char *ptoken, *patt;
    char *pattern;
    char buffer[AY_MAX_IDENT_SIZE];

    assert(tran && tran->origin);

    /* Allocate enough memory space for ay_transl.substr. */
    cnt = ay_pattern_idents_count(tran->origin);
    LY_ARRAY_CREATE_RET(NULL, tran->substr, cnt, AYE_MEMORY);

    pattern = ay_pattern_remove_parentheses(tran->origin);
    AY_CHECK_COND(!pattern, AYE_MEMORY);

    ret = 0;
    patt = pattern;
    while ((ptoken = ay_pattern_union_token(patt, 0, &len))) {
        if (!ay_pattern_union_token_is_valid(ptoken, len)) {
            goto fail;
        }
        if (memchr(ptoken, '?', len) && memchr(ptoken, '|', len)) {
            ret = ay_pattern_identifier_vbar_qm(ptoken, len, buffer, tran);
        } else if (memchr(ptoken, '?', len)) {
            ret = ay_pattern_identifier_qm(ptoken, len, buffer, tran);
        } else if (ay_ident_character_nocase(ptoken)) {
            ret = ay_pattern_identifier_nocase(ptoken, len, buffer, tran);
        } else {
            ret = ay_pattern_identifier_vbar(ptoken, len, buffer, tran);
        }
        AY_CHECK_GOTO(ret, clean);
        patt = ptoken + len;
    }

clean:
    free(pattern);

    return ret;

fail:
    ay_transl_table_substr_free(tran);
    tran->substr = NULL;
    ret = -1;
    goto clean;
}

/**
 * @brief Release ay_transl.substr in translation record.
 *
 * @param[in] entry Record from translation table.
 */
void
ay_transl_table_substr_free(struct ay_transl *entry)
{
    LY_ARRAY_COUNT_TYPE j;

    LY_ARRAY_FOR(entry->substr, j) {
        free(entry->substr[j]);
    }
    LY_ARRAY_FREE(entry->substr);
}
