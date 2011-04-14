/*
 * ctf-visitor-semantic-validator.c
 *
 * Common Trace Format Metadata Semantic Validator.
 *
 * Copyright 2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <glib.h>
#include <inttypes.h>
#include <errno.h>
#include <babeltrace/list.h>
#include "ctf-scanner.h"
#include "ctf-parser.h"
#include "ctf-ast.h"

#define _cds_list_first_entry(ptr, type, member)	\
	cds_list_entry((ptr)->next, type, member)

#define fprintf_dbg(fd, fmt, args...)	fprintf(fd, "%s: " fmt, __func__, ## args)

static
int _ctf_visitor_semantic_check(FILE *fd, int depth, struct ctf_node *node);

static
int ctf_visitor_unary_expression(FILE *fd, int depth, struct ctf_node *node)
{
	struct ctf_node *iter;
	int is_ctf_exp = 0, is_ctf_exp_left = 0;

	switch (node->parent->type) {
	case NODE_CTF_EXPRESSION:
		is_ctf_exp = 1;
		cds_list_for_each_entry(iter, &node->parent->u.ctf_expression.left,
					siblings) {
			if (iter == node) {
				is_ctf_exp_left = 1;
				/*
				 * We are a left child of a ctf expression.
				 * We are only allowed to be a string.
				 */
				if (node->u.unary_expression.type != UNARY_STRING) {
					fprintf(fd, "[error]: semantic error (left child of a ctf expression is only allowed to be a string)\n");

					goto errperm;
				}
				break;
			}
		}
		/* Right child of a ctf expression can be any type of unary exp. */
		break;			/* OK */
	case NODE_TYPE_DECLARATOR:
	case NODE_ENUM:
		/*
		 * We are the length of a type declarator or the size of an enum
		 * container. We can only be a numeric constant.
		 */
		switch (node->u.unary_expression.type) {
		case UNARY_SIGNED_CONSTANT:
		case UNARY_UNSIGNED_CONSTANT:
			break;
		default:
			fprintf(fd, "[error]: semantic error (children of type declarator and enum can only be numeric constants)\n");
			goto errperm;
		}
		break;			/* OK */
	case NODE_ENUMERATOR:
		/* The enumerator's parent has validated its validity already. */
		break;			/* OK */

	case NODE_UNARY_EXPRESSION:
		/*
		 * We disallow nested unary expressions and "sbrac" unary
		 * expressions.
		 */
		fprintf(fd, "[error]: semantic error (nested unary expressions not allowed ( () and [] ))\n");
		goto errperm;

	case NODE_ROOT:
	case NODE_EVENT:
	case NODE_STREAM:
	case NODE_TRACE:
	case NODE_TYPEDEF:
	case NODE_TYPEALIAS_TARGET:
	case NODE_TYPEALIAS_ALIAS:
	case NODE_TYPEALIAS:
	case NODE_TYPE_SPECIFIER:
	case NODE_POINTER:
	case NODE_FLOATING_POINT:
	case NODE_INTEGER:
	case NODE_STRING:
	case NODE_STRUCT_OR_VARIANT_DECLARATION:
	case NODE_VARIANT:
	case NODE_STRUCT:
	default:
		goto errinval;
	}

	switch (node->u.unary_expression.link) {
	case UNARY_LINK_UNKNOWN:
		/* We don't allow empty link except on the first node of the list */
		if (is_ctf_exp && _cds_list_first_entry(is_ctf_exp_left ?
					  &node->parent->u.ctf_expression.left :
					  &node->parent->u.ctf_expression.right,
					  struct ctf_node,
					  siblings) != node) {
			fprintf(fd, "[error]: semantic error (empty link not allowed except on first node of unary expression (need to separate nodes with \".\" or \"->\")\n");
			goto errperm;
		}
		break;			/* OK */
	case UNARY_DOTLINK:
	case UNARY_ARROWLINK:
		/* We only allow -> and . links between children of ctf_expression. */
		if (node->parent->type != NODE_CTF_EXPRESSION) {
			fprintf(fd, "[error]: semantic error (links \".\" and \"->\" are only allowed as children of ctf expression)\n");
			goto errperm;
		}
		/*
		 * Only strings can be separated linked by . or ->.
		 * This includes "", '' and non-quoted identifiers.
		 */
		if (node->u.unary_expression.type != UNARY_STRING) {
			fprintf(fd, "[error]: semantic error (links \".\" and \"->\" are only allowed to separate strings and identifiers)\n");
			goto errperm;
		}
		/* We don't allow link on the first node of the list */
		if (is_ctf_exp && _cds_list_first_entry(is_ctf_exp_left ?
					  &node->parent->u.ctf_expression.left :
					  &node->parent->u.ctf_expression.right,
					  struct ctf_node,
					  siblings) == node) {
			fprintf(fd, "[error]: semantic error (links \".\" and \"->\" are not allowed before first node of the unary expression list)\n");
			goto errperm;
		}
		break;
	case UNARY_DOTDOTDOT:
		/* We only allow ... link between children of enumerator. */
		if (node->parent->type != NODE_ENUMERATOR) {
			fprintf(fd, "[error]: semantic error (link \"...\" is only allowed within enumerator)\n");
			goto errperm;
		}
		/* We don't allow link on the first node of the list */
		if (_cds_list_first_entry(&node->parent->u.enumerator.values,
					  struct ctf_node,
					  siblings) == node) {
			fprintf(fd, "[error]: semantic error (link \"...\" is not allowed on the first node of the unary expression list)\n");
			goto errperm;
		}
		break;
	default:
		fprintf(fd, "[error] %s: unknown expression link type %d\n", __func__,
			(int) node->u.unary_expression.link);
		return -EINVAL;
	}
	return 0;

errinval:
	fprintf(fd, "[error] %s: incoherent parent type %s for node type %s\n", __func__,
		node_type(node->parent), node_type(node));
	return -EINVAL;		/* Incoherent structure */

errperm:
	fprintf(fd, "[error] %s: semantic error (parent type %s for node type %s)\n", __func__,
		node_type(node->parent), node_type(node));
	return -EPERM;		/* Structure not allowed */
}

static
int ctf_visitor_type_specifier(FILE *fd, int depth, struct ctf_node *node)
{
	switch (node->parent->type) {
	case NODE_CTF_EXPRESSION:
	case NODE_TYPE_DECLARATOR:
	case NODE_TYPEDEF:
	case NODE_TYPEALIAS_TARGET:
	case NODE_TYPEALIAS_ALIAS:
	case NODE_ENUM:
	case NODE_STRUCT_OR_VARIANT_DECLARATION:
		break;			/* OK */

	case NODE_ROOT:
	case NODE_EVENT:
	case NODE_STREAM:
	case NODE_TRACE:
	case NODE_UNARY_EXPRESSION:
	case NODE_TYPEALIAS:
	case NODE_TYPE_SPECIFIER:
	case NODE_POINTER:
	case NODE_FLOATING_POINT:
	case NODE_INTEGER:
	case NODE_STRING:
	case NODE_ENUMERATOR:
	case NODE_VARIANT:
	case NODE_STRUCT:
	default:
		goto errinval;
	}
	return 0;
errinval:
	fprintf(fd, "[error] %s: incoherent parent type %s for node type %s\n", __func__,
		node_type(node->parent), node_type(node));
	return -EINVAL;		/* Incoherent structure */
}

static
int ctf_visitor_type_declarator(FILE *fd, int depth, struct ctf_node *node)
{
	int ret = 0;
	struct ctf_node *iter;

	depth++;

	switch (node->parent->type) {
	case NODE_TYPE_DECLARATOR:
		/*
		 * A nested type declarator is not allowed to contain pointers.
		 */
		if (!cds_list_empty(&node->u.type_declarator.pointers))
			goto errperm;
		/* Fall-through */
	case NODE_TYPEDEF:
	case NODE_TYPEALIAS_TARGET:
	case NODE_TYPEALIAS_ALIAS:
	case NODE_STRUCT_OR_VARIANT_DECLARATION:
		break;			/* OK */

	case NODE_ROOT:
	case NODE_EVENT:
	case NODE_STREAM:
	case NODE_TRACE:
	case NODE_CTF_EXPRESSION:
	case NODE_UNARY_EXPRESSION:
	case NODE_TYPEALIAS:
	case NODE_TYPE_SPECIFIER:
	case NODE_POINTER:
	case NODE_FLOATING_POINT:
	case NODE_INTEGER:
	case NODE_STRING:
	case NODE_ENUMERATOR:
	case NODE_ENUM:
	case NODE_VARIANT:
	case NODE_STRUCT:
	default:
		goto errinval;
	}

	if (!cds_list_empty(&node->u.type_declarator.pointers)) {
		cds_list_for_each_entry(iter, &node->u.type_declarator.pointers,
					siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
	}

	switch (node->u.type_declarator.type) {
	case TYPEDEC_ID:
		break;
	case TYPEDEC_NESTED:
		if (node->u.type_declarator.u.nested.type_declarator) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1,
				node->u.type_declarator.u.nested.type_declarator);
			if (ret)
				return ret;
		}
		if (node->u.type_declarator.u.nested.length) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1,
				node->u.type_declarator.u.nested.length);
			if (ret)
				return ret;
		}
		if (node->u.type_declarator.bitfield_len) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1,
				node->u.type_declarator.bitfield_len);
			if (ret)
				return ret;
		}
		break;
	case TYPEDEC_UNKNOWN:
	default:
		fprintf(fd, "[error] %s: unknown type declarator %d\n", __func__,
			(int) node->u.type_declarator.type);
		return -EINVAL;
	}
	depth--;
	return 0;

errinval:
	fprintf(fd, "[error] %s: incoherent parent type %s for node type %s\n", __func__,
		node_type(node->parent), node_type(node));
	return -EINVAL;		/* Incoherent structure */

errperm:
	fprintf(fd, "[error] %s: semantic error (parent type %s for node type %s)\n", __func__,
		node_type(node->parent), node_type(node));
	return -EPERM;		/* Structure not allowed */
}

static
int _ctf_visitor_semantic_check(FILE *fd, int depth, struct ctf_node *node)
{
	int ret = 0;
	struct ctf_node *iter;

	switch (node->type) {
	case NODE_ROOT:
		cds_list_for_each_entry(iter, &node->u.root._typedef,
					siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.typealias,
					siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.declaration_specifier, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.trace, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.stream, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.root.event, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;

	case NODE_EVENT:
		switch (node->parent->type) {
		case NODE_ROOT:
			break;			/* OK */
		default:
			goto errinval;
		}

		cds_list_for_each_entry(iter, &node->u.event.declaration_list, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_STREAM:
		switch (node->parent->type) {
		case NODE_ROOT:
			break;			/* OK */
		default:
			goto errinval;
		}

		cds_list_for_each_entry(iter, &node->u.stream.declaration_list, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_TRACE:
		switch (node->parent->type) {
		case NODE_ROOT:
			break;			/* OK */
		default:
			goto errinval;
		}

		cds_list_for_each_entry(iter, &node->u.trace.declaration_list, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;

	case NODE_CTF_EXPRESSION:
		switch (node->parent->type) {
		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
			break;			/* OK */

		case NODE_CTF_EXPRESSION:
		case NODE_UNARY_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;
		}

		depth++;
		cds_list_for_each_entry(iter, &node->u.ctf_expression.left, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.ctf_expression.right, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		depth--;
		break;
	case NODE_UNARY_EXPRESSION:
		return ctf_visitor_unary_expression(fd, depth, node);

	case NODE_TYPEDEF:
		switch (node->parent->type) {
		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_VARIANT:
		case NODE_STRUCT:
			break;			/* OK */

		case NODE_CTF_EXPRESSION:
		case NODE_UNARY_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_TYPEALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		default:
			goto errinval;
		}

		depth++;
		cds_list_for_each_entry(iter, &node->u._typedef.declaration_specifier, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u._typedef.type_declarators, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		depth--;
		break;
	case NODE_TYPEALIAS_TARGET:
		switch (node->parent->type) {
		case NODE_TYPEALIAS:
			break;			/* OK */
		default:
			goto errinval;
		}

		depth++;
		cds_list_for_each_entry(iter, &node->u.typealias_target.declaration_specifier, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.typealias_target.type_declarators, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		depth--;
		break;
	case NODE_TYPEALIAS_ALIAS:
		switch (node->parent->type) {
		case NODE_TYPEALIAS:
			break;			/* OK */
		default:
			goto errinval;
		}

		depth++;
		cds_list_for_each_entry(iter, &node->u.typealias_alias.declaration_specifier, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.typealias_alias.type_declarators, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		depth--;
		break;
	case NODE_TYPEALIAS:
		switch (node->parent->type) {
		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_VARIANT:
		case NODE_STRUCT:
			break;			/* OK */

		case NODE_CTF_EXPRESSION:
		case NODE_UNARY_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_TYPEALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		default:
			goto errinval;
		}

		ret = _ctf_visitor_semantic_check(fd, depth + 1, node->u.typealias.target);
		if (ret)
			return ret;
		ret = _ctf_visitor_semantic_check(fd, depth + 1, node->u.typealias.alias);
		if (ret)
			return ret;
		break;

	case NODE_TYPE_SPECIFIER:
		ret = ctf_visitor_type_specifier(fd, depth, node);
		if (ret)
			return ret;
		break;
	case NODE_POINTER:
		switch (node->parent->type) {
		case NODE_TYPE_DECLARATOR:
			break;			/* OK */
		default:
			goto errinval;
		}
		break;
	case NODE_TYPE_DECLARATOR:
		ret = ctf_visitor_type_declarator(fd, depth, node);
		if (ret)
			return ret;
		break;

	case NODE_FLOATING_POINT:
		switch (node->parent->type) {
		case NODE_CTF_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
			break;			/* OK */

		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;

		case NODE_UNARY_EXPRESSION:
			goto errperm;
		}
		cds_list_for_each_entry(iter, &node->u.floating_point.expressions, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_INTEGER:
		switch (node->parent->type) {
		case NODE_CTF_EXPRESSION:
		case NODE_UNARY_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_TYPE_DECLARATOR:
		case NODE_ENUM:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
			break;			/* OK */

		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;

		}

		cds_list_for_each_entry(iter, &node->u.integer.expressions, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_STRING:
		switch (node->parent->type) {
		case NODE_CTF_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
			break;			/* OK */

		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;

		case NODE_UNARY_EXPRESSION:
			goto errperm;
		}

		cds_list_for_each_entry(iter, &node->u.string.expressions, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_ENUMERATOR:
		switch (node->parent->type) {
		case NODE_ENUM:
			break;
		default:
			goto errinval;
		}
		/*
		 * Enumerators are only allows to contain:
		 *    numeric unary expression
		 * or num. unary exp. ... num. unary exp
		 */
		{
			int count = 0;

			cds_list_for_each_entry(iter, &node->u.enumerator.values,
						siblings) {
				switch (count++) {
				case 0:	if (iter->type != NODE_UNARY_EXPRESSION
					    || (iter->u.unary_expression.type != UNARY_SIGNED_CONSTANT
						&& iter->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT)
					    || iter->u.unary_expression.link != UNARY_LINK_UNKNOWN) {
						fprintf(fd, "[error]: semantic error (first unary expression of enumerator is unexpected)\n");
						goto errperm;
					}
					break;
				case 1:	if (iter->type != NODE_UNARY_EXPRESSION
					    || (iter->u.unary_expression.type != UNARY_SIGNED_CONSTANT
						&& iter->u.unary_expression.type != UNARY_UNSIGNED_CONSTANT)
					    || iter->u.unary_expression.link != UNARY_DOTDOTDOT) {
						fprintf(fd, "[error]: semantic error (second unary expression of enumerator is unexpected)\n");
						goto errperm;
					}
					break;
				default:
					goto errperm;
				}
			}
		}

		cds_list_for_each_entry(iter, &node->u.enumerator.values, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_ENUM:
		switch (node->parent->type) {
		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_CTF_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_TYPE_DECLARATOR:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
			break;			/* OK */

		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;

		case NODE_UNARY_EXPRESSION:
			goto errperm;
		}

		depth++;
		if (node->u._enum.container_type) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, node->u._enum.container_type);
			if (ret)
				return ret;
		}

		cds_list_for_each_entry(iter, &node->u._enum.enumerator_list, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		depth--;
		break;
	case NODE_STRUCT_OR_VARIANT_DECLARATION:
		switch (node->parent->type) {
		case NODE_STRUCT:
		case NODE_VARIANT:
			break;
		default:
			goto errinval;
		}
		cds_list_for_each_entry(iter, &node->u.struct_or_variant_declaration.declaration_specifier, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		cds_list_for_each_entry(iter, &node->u.struct_or_variant_declaration.type_declarators, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;
	case NODE_VARIANT:
		switch (node->parent->type) {
		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_CTF_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
			break;			/* OK */

		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;

		case NODE_UNARY_EXPRESSION:
			goto errperm;
		}
		cds_list_for_each_entry(iter, &node->u.variant.declaration_list, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;

	case NODE_STRUCT:
		switch (node->parent->type) {
		case NODE_ROOT:
		case NODE_EVENT:
		case NODE_STREAM:
		case NODE_TRACE:
		case NODE_CTF_EXPRESSION:
		case NODE_TYPEDEF:
		case NODE_TYPEALIAS_TARGET:
		case NODE_TYPEALIAS_ALIAS:
		case NODE_STRUCT_OR_VARIANT_DECLARATION:
			break;			/* OK */

		case NODE_TYPEALIAS:
		case NODE_TYPE_SPECIFIER:
		case NODE_POINTER:
		case NODE_TYPE_DECLARATOR:
		case NODE_FLOATING_POINT:
		case NODE_INTEGER:
		case NODE_STRING:
		case NODE_ENUMERATOR:
		case NODE_ENUM:
		case NODE_VARIANT:
		case NODE_STRUCT:
		default:
			goto errinval;

		case NODE_UNARY_EXPRESSION:
			goto errperm;
		}
		cds_list_for_each_entry(iter, &node->u._struct.declaration_list, siblings) {
			ret = _ctf_visitor_semantic_check(fd, depth + 1, iter);
			if (ret)
				return ret;
		}
		break;

	case NODE_UNKNOWN:
	default:
		fprintf(fd, "[error] %s: unknown node type %d\n", __func__,
			(int) node->type);
		return -EINVAL;
	}
	return ret;

errinval:
	fprintf(fd, "[error] %s: incoherent parent type %s for node type %s\n", __func__,
		node_type(node->parent), node_type(node));
	return -EINVAL;		/* Incoherent structure */

errperm:
	fprintf(fd, "[error] %s: semantic error (parent type %s for node type %s)\n", __func__,
		node_type(node->parent), node_type(node));
	return -EPERM;		/* Structure not allowed */
}

int ctf_visitor_semantic_check(FILE *fd, int depth, struct ctf_node *node)
{
	int ret = 0;

	/*
	 * First make sure we create the parent links for all children. Let's
	 * take the safe route and recreate them at each validation, just in
	 * case the structure has changed.
	 */
	fprintf(fd, "CTF visitor: parent links creation... ");
	ret = ctf_visitor_parent_links(fd, depth, node);
	if (ret)
		return ret;
	fprintf(fd, "done.\n");
	fprintf(fd, "CTF visitor: semantic check... ");
	ret = _ctf_visitor_semantic_check(fd, depth, node);
	if (ret)
		return ret;
	fprintf(fd, "done.\n");
	return ret;
}