/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Copyright 2018 Michael Drake <tlsa@netsurf-browser.org>
 */

#ifndef css_select_mq_h_
#define css_select_mq_h_

#include <string.h>
#include <strings.h>

#include "bytecode/bytecode.h"
#include "select/helpers.h"
#include "select/strings.h"
#include "select/unit.h"

static inline bool
mq_match_feature_range_length_op1(css_mq_feature_op op,
				  const css_mq_value *value,
				  const css_fixed client_len,
				  const css_unit_ctx *unit_ctx)
{
	css_fixed v;

	if (value->type != CSS_MQ_VALUE_TYPE_DIM) {
		return false;
	}

	if (value->data.dim.unit != UNIT_PX) {
		v = css_unit_len2px_mq(unit_ctx,
				       value->data.dim.len,
				       css__to_css_unit(value->data.dim.unit));
	} else {
		v = value->data.dim.len;
	}

	switch (op) {
	case CSS_MQ_FEATURE_OP_BOOL:
		return false;
	case CSS_MQ_FEATURE_OP_LT:
		return v < client_len;
	case CSS_MQ_FEATURE_OP_LTE:
		return v <= client_len;
	case CSS_MQ_FEATURE_OP_EQ:
		return v == client_len;
	case CSS_MQ_FEATURE_OP_GTE:
		return v >= client_len;
	case CSS_MQ_FEATURE_OP_GT:
		return v > client_len;
	default:
		return false;
	}
}

static inline bool
mq_match_feature_range_length_op2(css_mq_feature_op op,
				  const css_mq_value *value,
				  const css_fixed client_len,
				  const css_unit_ctx *unit_ctx)
{
	css_fixed v;

	if (op == CSS_MQ_FEATURE_OP_UNUSED) {
		return true;
	}
	if (value->type != CSS_MQ_VALUE_TYPE_DIM) {
		return false;
	}

	if (value->data.dim.unit != UNIT_PX) {
		v = css_unit_len2px_mq(unit_ctx,
				       value->data.dim.len,
				       css__to_css_unit(value->data.dim.unit));
	} else {
		v = value->data.dim.len;
	}

	switch (op) {
	case CSS_MQ_FEATURE_OP_LT:
		return client_len < v;
	case CSS_MQ_FEATURE_OP_LTE:
		return client_len <= v;
	case CSS_MQ_FEATURE_OP_EQ:
		return client_len == v;
	case CSS_MQ_FEATURE_OP_GTE:
		return client_len >= v;
	case CSS_MQ_FEATURE_OP_GT:
		return client_len > v;
	default:
		return false;
	}
}

static inline bool mq_match_feature_eq_ident_op1(css_mq_feature_op op,
						 const css_mq_value *value,
						 const lwc_string *client_value)
{
	bool is_match;

	if (value->type != CSS_MQ_VALUE_TYPE_IDENT) {
		return false;
	}

	if (value->data.ident == NULL || client_value == NULL) {
		return false;
	}

	switch (op) {
	case CSS_MQ_FEATURE_OP_EQ:
		return (lwc_string_isequal(value->data.ident,
					   client_value,
					   &is_match) == lwc_error_ok) &&
		       is_match;
	default:
		return false;
	}
}

static inline bool mq_feature_named(lwc_string *name, const char *want)
{
	size_t len = strlen(want);

	return lwc_string_length(name) == len &&
	       strncasecmp(lwc_string_data(name), want, len) == 0;
}

static inline bool
mq_compare(css_mq_feature_op op, css_fixed v, css_fixed client)
{
	switch (op) {
	case CSS_MQ_FEATURE_OP_LT:
		return v < client;
	case CSS_MQ_FEATURE_OP_LTE:
		return v <= client;
	case CSS_MQ_FEATURE_OP_EQ:
		return v == client;
	case CSS_MQ_FEATURE_OP_GTE:
		return v >= client;
	case CSS_MQ_FEATURE_OP_GT:
		return v > client;
	default:
		return false;
	}
}

static inline bool mq_match_number(const css_mq_feature *feat, css_fixed client)
{
	if (feat->op == CSS_MQ_FEATURE_OP_BOOL) {
		return client != 0;
	}
	if (feat->value.type != CSS_MQ_VALUE_TYPE_NUM ||
	    !mq_compare(feat->op, feat->value.data.num_or_ratio, client)) {
		return false;
	}
	if (feat->op2 == CSS_MQ_FEATURE_OP_UNUSED) {
		return true;
	}
	return feat->value2.type == CSS_MQ_VALUE_TYPE_NUM &&
	       mq_compare(feat->op2, client, feat->value2.data.num_or_ratio);
}

static inline bool mq_ratio_value(const css_mq_value *value, css_fixed *ratio)
{
	if (value->type == CSS_MQ_VALUE_TYPE_RATIO ||
	    value->type == CSS_MQ_VALUE_TYPE_NUM) {
		*ratio = value->data.num_or_ratio;
		return true;
	}
	return false;
}

static inline bool mq_match_ratio(const css_mq_feature *feat, css_fixed client)
{
	css_fixed v;

	if (feat->op == CSS_MQ_FEATURE_OP_BOOL) {
		return client != 0;
	}
	if (!mq_ratio_value(&feat->value, &v) ||
	    !mq_compare(feat->op, v, client)) {
		return false;
	}
	if (feat->op2 == CSS_MQ_FEATURE_OP_UNUSED) {
		return true;
	}
	return mq_ratio_value(&feat->value2, &v) &&
	       mq_compare(feat->op2, client, v);
}

static inline bool
mq_resolution_value(const css_mq_value *value, css_fixed *dppx)
{
	if (value->type == CSS_MQ_VALUE_TYPE_NUM) {
		*dppx = value->data.num_or_ratio;
		return true;
	}
	if (value->type != CSS_MQ_VALUE_TYPE_DIM) {
		return false;
	}
	switch (value->data.dim.unit) {
	case UNIT_DPPX:
		*dppx = value->data.dim.len;
		return true;
	case UNIT_DPI:
		*dppx = FDIV(value->data.dim.len, INTTOFIX(96));
		return true;
	case UNIT_DPCM:
		*dppx = FDIV(FMUL(value->data.dim.len, FLTTOFIX(2.54)),
			     INTTOFIX(96));
		return true;
	default:
		return false;
	}
}

static inline bool
mq_match_resolution(const css_mq_feature *feat, css_fixed client)
{
	css_fixed v;

	if (feat->op == CSS_MQ_FEATURE_OP_BOOL) {
		return client != 0;
	}
	if (!mq_resolution_value(&feat->value, &v) ||
	    !mq_compare(feat->op, v, client)) {
		return false;
	}
	if (feat->op2 == CSS_MQ_FEATURE_OP_UNUSED) {
		return true;
	}
	return mq_resolution_value(&feat->value2, &v) &&
	       mq_compare(feat->op2, client, v);
}

static inline bool mq_match_keyword(const css_mq_feature *feat,
				    const char *const *names,
				    size_t count,
				    unsigned client,
				    unsigned none)
{
	if (feat->op == CSS_MQ_FEATURE_OP_BOOL) {
		return client != none;
	}
	if (feat->op != CSS_MQ_FEATURE_OP_EQ ||
	    feat->value.type != CSS_MQ_VALUE_TYPE_IDENT ||
	    feat->value.data.ident == NULL || client >= count) {
		return false;
	}
	return mq_feature_named(feat->value.data.ident, names[client]);
}

static inline bool
mq_match_extra_feature(const css_mq_feature *feat, const css_media *media)
{
	static const char *const orientation[] = {"portrait", "landscape"};
	static const char *const pointer[] = {"none", "coarse", "fine"};
	static const char *const hover[] = {"none", "on-demand", "hover"};
	static const char *const scan[] = {"progressive", "interlace"};
	static const char *const update[] = {"fast", "slow", "none"};
	static const char *const no_preference[] = {"no-preference"};
	lwc_string *name = feat->name;
	css_fixed dppx = media->resolution.value != 0 ? media->resolution.value
						      : INTTOFIX(1);
	css_fixed ratio = media->height != 0 ? FDIV(media->width, media->height)
					     : 0;

	if (mq_feature_named(name, "color")) {
		return mq_match_number(feat, media->color);
	} else if (mq_feature_named(name, "color-index")) {
		return mq_match_number(feat, media->color_index);
	} else if (mq_feature_named(name, "monochrome")) {
		return mq_match_number(feat, media->monochrome);
	} else if (mq_feature_named(name, "grid")) {
		return mq_match_number(feat, media->grid);
	} else if (mq_feature_named(name, "aspect-ratio") ||
		   mq_feature_named(name, "device-aspect-ratio")) {
		return mq_match_ratio(feat, ratio);
	} else if (mq_feature_named(name, "resolution") ||
		   mq_feature_named(name, "-webkit-device-pixel-ratio")) {
		return mq_match_resolution(feat, dppx);
	} else if (mq_feature_named(name, "orientation")) {
		return mq_match_keyword(
			feat, orientation, 2, media->orientation, 2);
	} else if (mq_feature_named(name, "pointer")) {
		return mq_match_keyword(feat,
					pointer,
					3,
					media->pointer,
					CSS_MEDIA_POINTER_NONE);
	} else if (mq_feature_named(name, "any-pointer")) {
		return mq_match_keyword(feat,
					pointer,
					3,
					media->any_pointer,
					CSS_MEDIA_POINTER_NONE);
	} else if (mq_feature_named(name, "hover")) {
		return mq_match_keyword(
			feat, hover, 3, media->hover, CSS_MEDIA_HOVER_NONE);
	} else if (mq_feature_named(name, "any-hover")) {
		return mq_match_keyword(
			feat, hover, 3, media->any_hover, CSS_MEDIA_HOVER_NONE);
	} else if (mq_feature_named(name, "scan")) {
		return mq_match_keyword(feat, scan, 2, media->scan, 2);
	} else if (mq_feature_named(name, "update")) {
		return mq_match_keyword(feat,
					update,
					3,
					media->update,
					CSS_MEDIA_UPDATE_FREQUENCY_NONE);
	} else if (mq_feature_named(name, "prefers-reduced-motion") ||
		   mq_feature_named(name, "prefers-reduced-transparency") ||
		   mq_feature_named(name, "prefers-reduced-data") ||
		   mq_feature_named(name, "prefers-contrast")) {
		return mq_match_keyword(feat, no_preference, 1, 0, 0);
	}
	return false;
}

/**
 * Match media query features.
 *
 * \param[in] feat      Condition to match.
 * \param[in] unit_ctx  Current unit conversion context.
 * \param[in] media     Current media spec, to check against feat.
 * \return true if condition matches, otherwise false.
 */
static inline bool mq_match_feature(const css_mq_feature *feat,
				    const css_unit_ctx *unit_ctx,
				    const css_media *media,
				    const css_select_strings *str)
{
	bool match;

	/* TODO: Use interned string for comparison. */
	if ((lwc_string_isequal(feat->name, str->width, &match) ==
		     lwc_error_ok &&
	     match == true) ||
	    mq_feature_named(feat->name, "device-width")) {
		if (!mq_match_feature_range_length_op1(
			    feat->op, &feat->value, media->width, unit_ctx)) {
			return false;
		}
		return mq_match_feature_range_length_op2(
			feat->op2, &feat->value2, media->width, unit_ctx);

	} else if ((lwc_string_isequal(feat->name, str->height, &match) ==
			    lwc_error_ok &&
		    match == true) ||
		   mq_feature_named(feat->name, "device-height")) {
		if (!mq_match_feature_range_length_op1(
			    feat->op, &feat->value, media->height, unit_ctx)) {
			return false;
		}

		return mq_match_feature_range_length_op2(
			feat->op2, &feat->value2, media->height, unit_ctx);

	} else if (lwc_string_isequal(feat->name,
				      str->prefers_color_scheme,
				      &match) == lwc_error_ok &&
		   match == true) {
		if (mq_match_feature_eq_ident_op1(
			    feat->op,
			    &feat->value,
			    media->prefers_color_scheme) ||
		    feat->op == CSS_MQ_FEATURE_OP_BOOL) {
			return true;
		}

		return false;
	}

	return mq_match_extra_feature(feat, media);
}

/**
 * Match media query conditions.
 *
 * \param[in] cond      Condition to match.
 * \param[in] unit_ctx  Current unit conversion context.
 * \param[in] media     Current media spec, to check against cond.
 * \return true if condition matches, otherwise false.
 */
static inline bool mq_match_condition(const css_mq_cond *cond,
				      const css_unit_ctx *unit_ctx,
				      const css_media *media,
				      const css_select_strings *str)
{
	bool matched = !cond->op;

	for (uint32_t i = 0; i < cond->nparts; i++) {
		bool part_matched;
		if (cond->parts[i]->type == CSS_MQ_FEATURE) {
			part_matched = mq_match_feature(
				cond->parts[i]->data.feat,
				unit_ctx,
				media,
				str);
		} else {
			assert(cond->parts[i]->type == CSS_MQ_COND);
			part_matched = mq_match_condition(
				cond->parts[i]->data.cond,
				unit_ctx,
				media,
				str);
		}

		if (cond->op) {
			/* OR */
			matched |= part_matched;
			if (matched) {
				break; /* Short-circuit */
			}
		} else {
			/* AND */
			matched &= part_matched;
			if (!matched) {
				break; /* Short-circuit */
			}
		}
	}

	return matched != cond->negate;
}

/**
 * Test whether media query list matches current media.
 *
 * If anything in the list matches, the list matches.  If none match
 * it doesn't match.
 *
 * \param[in] m         Media query list.
 * \param[in] unit_ctx  Current unit conversion context.
 * \param[in] media     Current media spec, to check against m.
 * \return true if media query list matches media
 */
static inline bool mq__list_match(const css_mq_query *m,
				  const css_unit_ctx *unit_ctx,
				  const css_media *media,
				  const css_select_strings *str)
{
	for (; m != NULL; m = m->next) {
		bool match = (m->type & media->type) != 0 &&
			     (m->cond == NULL ||
			      mq_match_condition(
				      m->cond, unit_ctx, media, str));

		if (match != m->negate_type) {
			return true;
		}
	}

	return false;
}

/**
 * Test whether the rule applies for current media.
 *
 * \param rule      Rule to test
 * \param unit_ctx  Current unit conversion context.
 * \param media     Current media spec
 * \return true iff chain's rule applies for media
 */
static inline bool mq_rule_good_for_media(const css_rule *rule,
					  const css_unit_ctx *unit_ctx,
					  const css_media *media,
					  const css_select_strings *str)
{
	bool applies = true;
	const css_rule *ancestor = rule;

	while (ancestor != NULL) {
		const css_rule_media *m = (const css_rule_media *)ancestor;

		if (ancestor->type == CSS_RULE_MEDIA) {
			applies = mq__list_match(
				m->media, unit_ctx, media, str);
			if (applies == false) {
				break;
			}
		}

		if (ancestor->ptype != CSS_RULE_PARENT_STYLESHEET) {
			ancestor = ancestor->parent;
		} else {
			ancestor = NULL;
		}
	}

	return applies;
}

#endif
