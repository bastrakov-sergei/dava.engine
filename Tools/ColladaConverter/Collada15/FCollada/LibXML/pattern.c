/*
 * pattern.c: Implemetation of selectors for nodes
 *
 * Reference:
 *   http://www.w3.org/TR/2001/REC-xmlschema-1-20010502/
 *   to some extent 
 *   http://www.w3.org/TR/1999/REC-xml-19991116
 *
 * See Copyright for the status of this software.
 *
 * daniel@veillard.com
 */

/*
 * TODO:
 * - compilation flags to check for specific syntaxes
 *   using flags of xmlPatterncompile()
 * - making clear how pattern starting with / or . need to be handled,
 *   currently push(NULL, NULL) means a reset of the streaming context
 *   and indicating we are on / (the document node), probably need
 *   something similar for .
 * - get rid of the "compile" starting with lowercase
 * - get rid of the Strdup/Strndup in case of dictionary
 */

#define IN_LIBXML
#include "libxml.h"

#include <string.h>
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/hash.h>
#include <libxml/dict.h>
#include <libxml/xmlerror.h>
#include <libxml/parserInternals.h>
#include <libxml/pattern.h>

#ifdef LIBXML_PATTERN_ENABLED

/* #define DEBUG_STREAMING */
#define SUPPORT_IDC

#define ERROR(a, b, c, d)
#define ERROR5(a, b, c, d, e)

#define XML_STREAM_STEP_DESC 1
#define XML_STREAM_STEP_FINAL 2
#define XML_STREAM_STEP_ROOT 4
#define XML_STREAM_STEP_ATTR 8

#define XML_PATTERN_NOTPATTERN 1

typedef struct _xmlStreamStep xmlStreamStep;
typedef xmlStreamStep* xmlStreamStepPtr;
struct _xmlStreamStep
{
    int flags; /* properties of that step */
    const xmlChar* name; /* first string value if NULL accept all */
    const xmlChar* ns; /* second string value */
};

typedef struct _xmlStreamComp xmlStreamComp;
typedef xmlStreamComp* xmlStreamCompPtr;
struct _xmlStreamComp
{
    xmlDict* dict; /* the dictionnary if any */
    int nbStep; /* number of steps in the automata */
    int maxStep; /* allocated number of steps */
    xmlStreamStepPtr steps; /* the array of steps */
};

struct _xmlStreamCtxt
{
    struct _xmlStreamCtxt* next; /* link to next sub pattern if | */
    xmlStreamCompPtr comp; /* the compiled stream */
    int nbState; /* number of state in the automata */
    int maxState; /* allocated number of state */
    int level; /* how deep are we ? */
    int* states; /* the array of step indexes */
    int flags; /* validation options */
};

static void xmlFreeStreamComp(xmlStreamCompPtr comp);

/*
 * Types are private:
 */

typedef enum {
    XML_OP_END = 0,
    XML_OP_ROOT,
    XML_OP_ELEM,
    XML_OP_CHILD,
    XML_OP_ATTR,
    XML_OP_PARENT,
    XML_OP_ANCESTOR,
    XML_OP_NS,
    XML_OP_ALL
} xmlPatOp;

typedef struct _xmlStepState xmlStepState;
typedef xmlStepState* xmlStepStatePtr;
struct _xmlStepState
{
    int step;
    xmlNodePtr node;
};

typedef struct _xmlStepStates xmlStepStates;
typedef xmlStepStates* xmlStepStatesPtr;
struct _xmlStepStates
{
    int nbstates;
    int maxstates;
    xmlStepStatePtr states;
};

typedef struct _xmlStepOp xmlStepOp;
typedef xmlStepOp* xmlStepOpPtr;
struct _xmlStepOp
{
    xmlPatOp op;
    const xmlChar* value;
    const xmlChar* value2;
};

#define PAT_FROM_ROOT 1
#define PAT_FROM_CUR 2

struct _xmlPattern
{
    void* data; /* the associated template */
    xmlDictPtr dict; /* the optional dictionnary */
    struct _xmlPattern* next; /* next pattern if | is used */
    const xmlChar* pattern; /* the pattern */

    int flags; /* flags */
    int nbStep;
    int maxStep;
    xmlStepOpPtr steps; /* ops for computation */
    xmlStreamCompPtr stream; /* the streaming data if any */
};

typedef struct _xmlPatParserContext xmlPatParserContext;
typedef xmlPatParserContext* xmlPatParserContextPtr;
struct _xmlPatParserContext
{
    const xmlChar* cur; /* the current char being parsed */
    const xmlChar* base; /* the full expression */
    int error; /* error code */
    xmlDictPtr dict; /* the dictionnary if any */
    xmlPatternPtr comp; /* the result */
    xmlNodePtr elem; /* the current node if any */
    const xmlChar** namespaces; /* the namespaces definitions */
    int nb_namespaces; /* the number of namespaces */
};

/************************************************************************
 * 									*
 * 			Type functions 					*
 * 									*
 ************************************************************************/

/**
 * xmlNewPattern:
 *
 * Create a new XSLT Pattern
 *
 * Returns the newly allocated xmlPatternPtr or NULL in case of error
 */
static xmlPatternPtr
xmlNewPattern(void)
{
    xmlPatternPtr cur;

    cur = (xmlPatternPtr)xmlMalloc(sizeof(xmlPattern));
    if (cur == NULL)
    {
        ERROR(NULL, NULL, NULL,
              "xmlNewPattern : malloc failed\n");
        return (NULL);
    }
    memset(cur, 0, sizeof(xmlPattern));
    cur->maxStep = 10;
    cur->steps = (xmlStepOpPtr)xmlMalloc(cur->maxStep * sizeof(xmlStepOp));
    if (cur->steps == NULL)
    {
        xmlFree(cur);
        ERROR(NULL, NULL, NULL,
              "xmlNewPattern : malloc failed\n");
        return (NULL);
    }
    return (cur);
}

/**
 * xmlFreePattern:
 * @comp:  an XSLT comp
 *
 * Free up the memory allocated by @comp
 */
void
xmlFreePattern(xmlPatternPtr comp)
{
    xmlStepOpPtr op;
    int i;

    if (comp == NULL)
        return;
    if (comp->next != NULL)
        xmlFreePattern(comp->next);
    if (comp->stream != NULL)
        xmlFreeStreamComp(comp->stream);
    if (comp->pattern != NULL)
        xmlFree((xmlChar*)comp->pattern);
    if (comp->steps != NULL)
    {
        if (comp->dict == NULL)
        {
            for (i = 0; i < comp->nbStep; i++)
            {
                op = &comp->steps[i];
                if (op->value != NULL)
                    xmlFree((xmlChar*)op->value);
                if (op->value2 != NULL)
                    xmlFree((xmlChar*)op->value2);
            }
        }
        xmlFree(comp->steps);
    }
    if (comp->dict != NULL)
        xmlDictFree(comp->dict);

    memset(comp, -1, sizeof(xmlPattern));
    xmlFree(comp);
}

/**
 * xmlFreePatternList:
 * @comp:  an XSLT comp list
 *
 * Free up the memory allocated by all the elements of @comp
 */
void
xmlFreePatternList(xmlPatternPtr comp)
{
    xmlPatternPtr cur;

    while (comp != NULL)
    {
        cur = comp;
        comp = comp->next;
        cur->next = NULL;
        xmlFreePattern(cur);
    }
}

/**
 * xmlNewPatParserContext:
 * @pattern:  the pattern context
 * @dict:  the inherited dictionnary or NULL
 * @namespaces: the prefix definitions, array of [URI, prefix] terminated
 *              with [NULL, NULL] or NULL if no namespace is used
 *
 * Create a new XML pattern parser context
 *
 * Returns the newly allocated xmlPatParserContextPtr or NULL in case of error
 */
static xmlPatParserContextPtr
xmlNewPatParserContext(const xmlChar* pattern, xmlDictPtr dict,
                       const xmlChar** namespaces)
{
    xmlPatParserContextPtr cur;

    if (pattern == NULL)
        return (NULL);

    cur = (xmlPatParserContextPtr)xmlMalloc(sizeof(xmlPatParserContext));
    if (cur == NULL)
    {
        ERROR(NULL, NULL, NULL,
              "xmlNewPatParserContext : malloc failed\n");
        return (NULL);
    }
    memset(cur, 0, sizeof(xmlPatParserContext));
    cur->dict = dict;
    cur->cur = pattern;
    cur->base = pattern;
    if (namespaces != NULL)
    {
        int i;
        for (i = 0; namespaces[2 * i] != NULL; i++)
            ;
        cur->nb_namespaces = i;
    }
    else
    {
        cur->nb_namespaces = 0;
    }
    cur->namespaces = namespaces;
    return (cur);
}

/**
 * xmlFreePatParserContext:
 * @ctxt:  an XSLT parser context
 *
 * Free up the memory allocated by @ctxt
 */
static void
xmlFreePatParserContext(xmlPatParserContextPtr ctxt)
{
    if (ctxt == NULL)
        return;
    memset(ctxt, -1, sizeof(xmlPatParserContext));
    xmlFree(ctxt);
}

/**
 * xmlPatternAdd:
 * @comp:  the compiled match expression
 * @op:  an op
 * @value:  the first value
 * @value2:  the second value
 *
 * Add an step to an XSLT Compiled Match
 *
 * Returns -1 in case of failure, 0 otherwise.
 */
static int
xmlPatternAdd(xmlPatParserContextPtr ctxt ATTRIBUTE_UNUSED,
              xmlPatternPtr comp,
              xmlPatOp op, xmlChar* value, xmlChar* value2)
{
    if (comp->nbStep >= comp->maxStep)
    {
        xmlStepOpPtr temp;
        temp = (xmlStepOpPtr)xmlRealloc(comp->steps, comp->maxStep * 2 *
                                        sizeof(xmlStepOp));
        if (temp == NULL)
        {
            ERROR(ctxt, NULL, NULL,
                  "xmlPatternAdd: realloc failed\n");
            return (-1);
        }
        comp->steps = temp;
        comp->maxStep *= 2;
    }
    comp->steps[comp->nbStep].op = op;
    comp->steps[comp->nbStep].value = value;
    comp->steps[comp->nbStep].value2 = value2;
    comp->nbStep++;
    return (0);
}

#if 0
/**
 * xsltSwapTopPattern:
 * @comp:  the compiled match expression
 *
 * reverse the two top steps.
 */
static void
xsltSwapTopPattern(xmlPatternPtr comp) {
    int i;
    int j = comp->nbStep - 1;

    if (j > 0) {
	register const xmlChar *tmp;
	register xmlPatOp op;
	i = j - 1;
	tmp = comp->steps[i].value;
	comp->steps[i].value = comp->steps[j].value;
	comp->steps[j].value = tmp;
	tmp = comp->steps[i].value2;
	comp->steps[i].value2 = comp->steps[j].value2;
	comp->steps[j].value2 = tmp;
	op = comp->steps[i].op;
	comp->steps[i].op = comp->steps[j].op;
	comp->steps[j].op = op;
    }
}
#endif

/**
 * xmlReversePattern:
 * @comp:  the compiled match expression
 *
 * reverse all the stack of expressions
 *
 * returns 0 in case of success and -1 in case of error.
 */
static int
xmlReversePattern(xmlPatternPtr comp)
{
    int i, j;

    /*
     * remove the leading // for //a or .//a
     */
    if ((comp->nbStep > 0) && (comp->steps[0].op == XML_OP_ANCESTOR))
    {
        for (i = 0, j = 1; j < comp->nbStep; i++, j++)
        {
            comp->steps[i].value = comp->steps[j].value;
            comp->steps[i].value2 = comp->steps[j].value2;
            comp->steps[i].op = comp->steps[j].op;
        }
        comp->nbStep--;
    }
    if (comp->nbStep >= comp->maxStep)
    {
        xmlStepOpPtr temp;
        temp = (xmlStepOpPtr)xmlRealloc(comp->steps, comp->maxStep * 2 *
                                        sizeof(xmlStepOp));
        if (temp == NULL)
        {
            ERROR(ctxt, NULL, NULL,
                  "xmlReversePattern: realloc failed\n");
            return (-1);
        }
        comp->steps = temp;
        comp->maxStep *= 2;
    }
    i = 0;
    j = comp->nbStep - 1;
    while (j > i)
    {
        register const xmlChar* tmp;
        register xmlPatOp op;
        tmp = comp->steps[i].value;
        comp->steps[i].value = comp->steps[j].value;
        comp->steps[j].value = tmp;
        tmp = comp->steps[i].value2;
        comp->steps[i].value2 = comp->steps[j].value2;
        comp->steps[j].value2 = tmp;
        op = comp->steps[i].op;
        comp->steps[i].op = comp->steps[j].op;
        comp->steps[j].op = op;
        j--;
        i++;
    }
    comp->steps[comp->nbStep].value = NULL;
    comp->steps[comp->nbStep].value2 = NULL;
    comp->steps[comp->nbStep++].op = XML_OP_END;
    return (0);
}

/************************************************************************
 * 									*
 * 		The interpreter for the precompiled patterns		*
 * 									*
 ************************************************************************/

static int
xmlPatPushState(xmlStepStates* states, int step, xmlNodePtr node)
{
    if ((states->states == NULL) || (states->maxstates <= 0))
    {
        states->maxstates = 4;
        states->nbstates = 0;
        states->states = xmlMalloc(4 * sizeof(xmlStepState));
    }
    else if (states->maxstates <= states->nbstates)
    {
        xmlStepState* tmp;

        tmp = (xmlStepStatePtr)xmlRealloc(states->states,
                                          2 * states->maxstates * sizeof(xmlStepState));
        if (tmp == NULL)
            return (-1);
        states->states = tmp;
        states->maxstates *= 2;
    }
    states->states[states->nbstates].step = step;
    states->states[states->nbstates++].node = node;
#if 0
    fprintf(stderr, "Push: %d, %s\n", step, node->name);
#endif
    return (0);
}

/**
 * xmlPatMatch:
 * @comp: the precompiled pattern
 * @node: a node
 *
 * Test wether the node matches the pattern
 *
 * Returns 1 if it matches, 0 if it doesn't and -1 in case of failure
 */
static int
xmlPatMatch(xmlPatternPtr comp, xmlNodePtr node)
{
    int i;
    xmlStepOpPtr step;
    xmlStepStates states = { 0, 0, NULL }; /* // may require backtrack */

    if ((comp == NULL) || (node == NULL))
        return (-1);
    i = 0;
restart:
    for (; i < comp->nbStep; i++)
    {
        step = &comp->steps[i];
        switch (step->op)
        {
        case XML_OP_END:
            goto found;
        case XML_OP_ROOT:
            if (node->type == XML_NAMESPACE_DECL)
                goto rollback;
            node = node->parent;
            if ((node->type == XML_DOCUMENT_NODE) ||
#ifdef LIBXML_DOCB_ENABLED
                (node->type == XML_DOCB_DOCUMENT_NODE) ||
#endif
                (node->type == XML_HTML_DOCUMENT_NODE))
                continue;
            goto rollback;
        case XML_OP_ELEM:
            if (node->type != XML_ELEMENT_NODE)
                goto rollback;
            if (step->value == NULL)
                continue;
            if (step->value[0] != node->name[0])
                goto rollback;
            if (!xmlStrEqual(step->value, node->name))
                goto rollback;

            /* Namespace test */
            if (node->ns == NULL)
            {
                if (step->value2 != NULL)
                    goto rollback;
            }
            else if (node->ns->href != NULL)
            {
                if (step->value2 == NULL)
                    goto rollback;
                if (!xmlStrEqual(step->value2, node->ns->href))
                    goto rollback;
            }
            continue;
        case XML_OP_CHILD:
        {
            xmlNodePtr lst;

            if ((node->type != XML_ELEMENT_NODE) &&
                (node->type != XML_DOCUMENT_NODE) &&
#ifdef LIBXML_DOCB_ENABLED
                (node->type != XML_DOCB_DOCUMENT_NODE) &&
#endif
                (node->type != XML_HTML_DOCUMENT_NODE))
                goto rollback;

            lst = node->children;

            if (step->value != NULL)
            {
                while (lst != NULL)
                {
                    if ((lst->type == XML_ELEMENT_NODE) &&
                        (step->value[0] == lst->name[0]) &&
                        (xmlStrEqual(step->value, lst->name)))
                        break;
                    lst = lst->next;
                }
                if (lst != NULL)
                    continue;
            }
            goto rollback;
        }
        case XML_OP_ATTR:
            if (node->type != XML_ATTRIBUTE_NODE)
                goto rollback;
            if (step->value != NULL)
            {
                if (step->value[0] != node->name[0])
                    goto rollback;
                if (!xmlStrEqual(step->value, node->name))
                    goto rollback;
            }
            /* Namespace test */
            if (node->ns == NULL)
            {
                if (step->value2 != NULL)
                    goto rollback;
            }
            else if (step->value2 != NULL)
            {
                if (!xmlStrEqual(step->value2, node->ns->href))
                    goto rollback;
            }
            continue;
        case XML_OP_PARENT:
            if ((node->type == XML_DOCUMENT_NODE) ||
                (node->type == XML_HTML_DOCUMENT_NODE) ||
#ifdef LIBXML_DOCB_ENABLED
                (node->type == XML_DOCB_DOCUMENT_NODE) ||
#endif
                (node->type == XML_NAMESPACE_DECL))
                goto rollback;
            node = node->parent;
            if (node == NULL)
                goto rollback;
            if (step->value == NULL)
                continue;
            if (step->value[0] != node->name[0])
                goto rollback;
            if (!xmlStrEqual(step->value, node->name))
                goto rollback;
            /* Namespace test */
            if (node->ns == NULL)
            {
                if (step->value2 != NULL)
                    goto rollback;
            }
            else if (node->ns->href != NULL)
            {
                if (step->value2 == NULL)
                    goto rollback;
                if (!xmlStrEqual(step->value2, node->ns->href))
                    goto rollback;
            }
            continue;
        case XML_OP_ANCESTOR:
            /* TODO: implement coalescing of ANCESTOR/NODE ops */
            if (step->value == NULL)
            {
                i++;
                step = &comp->steps[i];
                if (step->op == XML_OP_ROOT)
                    goto found;
                if (step->op != XML_OP_ELEM)
                    goto rollback;
                if (step->value == NULL)
                    return (-1);
            }
            if (node == NULL)
                goto rollback;
            if ((node->type == XML_DOCUMENT_NODE) ||
                (node->type == XML_HTML_DOCUMENT_NODE) ||
#ifdef LIBXML_DOCB_ENABLED
                (node->type == XML_DOCB_DOCUMENT_NODE) ||
#endif
                (node->type == XML_NAMESPACE_DECL))
                goto rollback;
            node = node->parent;
            while (node != NULL)
            {
                if (node == NULL)
                    goto rollback;
                if ((node->type == XML_ELEMENT_NODE) &&
                    (step->value[0] == node->name[0]) &&
                    (xmlStrEqual(step->value, node->name)))
                {
                    /* Namespace test */
                    if (node->ns == NULL)
                    {
                        if (step->value2 == NULL)
                            break;
                    }
                    else if (node->ns->href != NULL)
                    {
                        if ((step->value2 != NULL) &&
                            (xmlStrEqual(step->value2, node->ns->href)))
                            break;
                    }
                }
                node = node->parent;
            }
            if (node == NULL)
                goto rollback;
            /*
		 * prepare a potential rollback from here
		 * for ancestors of that node.
		 */
            if (step->op == XML_OP_ANCESTOR)
                xmlPatPushState(&states, i, node);
            else
                xmlPatPushState(&states, i - 1, node);
            continue;
        case XML_OP_NS:
            if (node->type != XML_ELEMENT_NODE)
                goto rollback;
            if (node->ns == NULL)
            {
                if (step->value != NULL)
                    goto rollback;
            }
            else if (node->ns->href != NULL)
            {
                if (step->value == NULL)
                    goto rollback;
                if (!xmlStrEqual(step->value, node->ns->href))
                    goto rollback;
            }
            break;
        case XML_OP_ALL:
            if (node->type != XML_ELEMENT_NODE)
                goto rollback;
            break;
        }
    }
found:
    if (states.states != NULL)
    {
        /* Free the rollback states */
        xmlFree(states.states);
    }
    return (1);
rollback:
    /* got an error try to rollback */
    if (states.states == NULL)
        return (0);
    if (states.nbstates <= 0)
    {
        xmlFree(states.states);
        return (0);
    }
    states.nbstates--;
    i = states.states[states.nbstates].step;
    node = states.states[states.nbstates].node;
#if 0
    fprintf(stderr, "Pop: %d, %s\n", i, node->name);
#endif
    goto restart;
}

/************************************************************************
 *									*
 *			Dedicated parser for templates			*
 *									*
 ************************************************************************/

#define TODO 								\
    xmlGenericError(xmlGenericErrorContext,           \
                    "Unimplemented block at %s:%d\n", \
                    __FILE__, __LINE__);
#define CUR (*ctxt->cur)
#define SKIP(val) ctxt->cur += (val)
#define NXT(val) ctxt->cur[(val)]
#define CUR_PTR ctxt->cur

#define SKIP_BLANKS 							\
    while (IS_BLANK_CH(CUR)) NEXT

#define CURRENT (*ctxt->cur)
#define NEXT ((*ctxt->cur) ? ctxt->cur++ : ctxt->cur)


#define PUSH(op, val, val2) 						\
    if (xmlPatternAdd(ctxt, ctxt->comp, (op), (val), (val2))) goto error;

#define XSLT_ERROR(X)							\
    { xsltError(ctxt, __FILE__, __LINE__, X);			\
      ctxt->error = (X); return; }

#define XSLT_ERROR0(X)							\
    { xsltError(ctxt, __FILE__, __LINE__, X);			\
      ctxt->error = (X); return (0); }

#if 0
/**
 * xmlPatScanLiteral:
 * @ctxt:  the XPath Parser context
 *
 * Parse an XPath Litteral:
 *
 * [29] Literal ::= '"' [^"]* '"'
 *                | "'" [^']* "'"
 *
 * Returns the Literal parsed or NULL
 */

static xmlChar *
xmlPatScanLiteral(xmlPatParserContextPtr ctxt) {
    const xmlChar *q, *cur;
    xmlChar *ret = NULL;
    int val, len;

    SKIP_BLANKS;
    if (CUR == '"') {
        NEXT;
	cur = q = CUR_PTR;
	val = xmlStringCurrentChar(NULL, cur, &len);
	while ((IS_CHAR(val)) && (val != '"')) {
	    cur += len;
	    val = xmlStringCurrentChar(NULL, cur, &len);
	}
	if (!IS_CHAR(val)) {
	    ctxt->error = 1;
	    return(NULL);
	} else {
	    ret = xmlStrndup(q, cur - q);
        }
	cur += len;
	CUR_PTR = cur;
    } else if (CUR == '\'') {
        NEXT;
	cur = q = CUR_PTR;
	val = xmlStringCurrentChar(NULL, cur, &len);
	while ((IS_CHAR(val)) && (val != '\'')) {
	    cur += len;
	    val = xmlStringCurrentChar(NULL, cur, &len);
	}
	if (!IS_CHAR(val)) {
	    ctxt->error = 1;
	    return(NULL);
	} else {
	    ret = xmlStrndup(q, cur - q);
        }
	cur += len;
	CUR_PTR = cur;
    } else {
	/* XP_ERROR(XPATH_START_LITERAL_ERROR); */
	ctxt->error = 1;
	return(NULL);
    }
    return(ret);
}
#endif

/**
 * xmlPatScanName:
 * @ctxt:  the XPath Parser context
 *
 * [4] NameChar ::= Letter | Digit | '.' | '-' | '_' | 
 *                  CombiningChar | Extender
 *
 * [5] Name ::= (Letter | '_' | ':') (NameChar)*
 *
 * [6] Names ::= Name (S Name)*
 *
 * Returns the Name parsed or NULL
 */

static xmlChar*
xmlPatScanName(xmlPatParserContextPtr ctxt)
{
    const xmlChar *q, *cur;
    xmlChar* ret = NULL;
    int val, len;

    SKIP_BLANKS;

    cur = q = CUR_PTR;
    val = xmlStringCurrentChar(NULL, cur, &len);
    if (!IS_LETTER(val) && (val != '_') && (val != ':'))
        return (NULL);

    while ((IS_LETTER(val)) || (IS_DIGIT(val)) ||
           (val == '.') || (val == '-') ||
           (val == '_') ||
           (IS_COMBINING(val)) ||
           (IS_EXTENDER(val)))
    {
        cur += len;
        val = xmlStringCurrentChar(NULL, cur, &len);
    }
    ret = xmlStrndup(q, cur - q);
    CUR_PTR = cur;
    return (ret);
}

/**
 * xmlPatScanNCName:
 * @ctxt:  the XPath Parser context
 *
 * Parses a non qualified name
 *
 * Returns the Name parsed or NULL
 */

static xmlChar*
xmlPatScanNCName(xmlPatParserContextPtr ctxt)
{
    const xmlChar *q, *cur;
    xmlChar* ret = NULL;
    int val, len;

    SKIP_BLANKS;

    cur = q = CUR_PTR;
    val = xmlStringCurrentChar(NULL, cur, &len);
    if (!IS_LETTER(val) && (val != '_'))
        return (NULL);

    while ((IS_LETTER(val)) || (IS_DIGIT(val)) ||
           (val == '.') || (val == '-') ||
           (val == '_') ||
           (IS_COMBINING(val)) ||
           (IS_EXTENDER(val)))
    {
        cur += len;
        val = xmlStringCurrentChar(NULL, cur, &len);
    }
    ret = xmlStrndup(q, cur - q);
    CUR_PTR = cur;
    return (ret);
}

#if 0
/**
 * xmlPatScanQName:
 * @ctxt:  the XPath Parser context
 * @prefix:  the place to store the prefix
 *
 * Parse a qualified name
 *
 * Returns the Name parsed or NULL
 */

static xmlChar *
xmlPatScanQName(xmlPatParserContextPtr ctxt, xmlChar **prefix) {
    xmlChar *ret = NULL;

    *prefix = NULL;
    ret = xmlPatScanNCName(ctxt);
    if (CUR == ':') {
        *prefix = ret;
	NEXT;
	ret = xmlPatScanNCName(ctxt);
    }
    return(ret);
}
#endif

/**
 * xmlCompileAttributeTest:
 * @ctxt:  the compilation context
 *
 * Compile an attribute test.
 */
static void
xmlCompileAttributeTest(xmlPatParserContextPtr ctxt)
{
    xmlChar* token = NULL;
    xmlChar* name = NULL;
    xmlChar* URL = NULL;

    name = xmlPatScanNCName(ctxt);
    if (name == NULL)
    {
        if (CUR == '*')
        {
            PUSH(XML_OP_ATTR, NULL, NULL);
        }
        else
        {
            ERROR(NULL, NULL, NULL,
                  "xmlCompileAttributeTest : Name expected\n");
            ctxt->error = 1;
        }
        return;
    }
    if (CUR == ':')
    {
        int i;
        xmlChar* prefix = name;

        NEXT;
        /*
	* This is a namespace match
	*/
        token = xmlPatScanName(ctxt);
        for (i = 0; i < ctxt->nb_namespaces; i++)
        {
            if (xmlStrEqual(ctxt->namespaces[2 * i + 1], prefix))
            {
                URL = xmlStrdup(ctxt->namespaces[2 * i]);
                break;
            }
        }
        if (i >= ctxt->nb_namespaces)
        {
            ERROR5(NULL, NULL, NULL,
                   "xmlCompileAttributeTest : no namespace bound to prefix %s\n",
                   prefix);
            ctxt->error = 1;
            goto error;
        }

        xmlFree(prefix);
        if (token == NULL)
        {
            if (CUR == '*')
            {
                NEXT;
                PUSH(XML_OP_ATTR, NULL, URL);
            }
            else
            {
                ERROR(NULL, NULL, NULL,
                      "xmlCompileAttributeTest : Name expected\n");
                ctxt->error = 1;
                goto error;
            }
        }
        else
        {
            PUSH(XML_OP_ATTR, token, URL);
        }
    }
    else
    {
        PUSH(XML_OP_ATTR, name, NULL);
    }
    return;
error:
    if (URL != NULL)
        xmlFree(URL);
    if (token != NULL)
        xmlFree(token);
}

/**
 * xmlCompileStepPattern:
 * @ctxt:  the compilation context
 *
 * Compile the Step Pattern and generates a precompiled
 * form suitable for fast matching.
 *
 * [3]    Step    ::=    '.' | NameTest
 * [4]    NameTest    ::=    QName | '*' | NCName ':' '*' 
 */

static void
xmlCompileStepPattern(xmlPatParserContextPtr ctxt)
{
    xmlChar* token = NULL;
    xmlChar* name = NULL;
    xmlChar* URL = NULL;

    SKIP_BLANKS;
    if (CUR == '.')
    {
        NEXT;
        PUSH(XML_OP_ELEM, NULL, NULL);
        return;
    }
    name = xmlPatScanNCName(ctxt);
    if (name == NULL)
    {
        if (CUR == '*')
        {
            NEXT;
            PUSH(XML_OP_ALL, NULL, NULL);
            return;
        }
        else if (CUR == '@')
        {
            NEXT;
            xmlCompileAttributeTest(ctxt);
            if (ctxt->error != 0)
                goto error;
            return;
        }
        else
        {
            ERROR(NULL, NULL, NULL,
                  "xmlCompileStepPattern : Name expected\n");
            ctxt->error = 1;
            return;
        }
    }
    SKIP_BLANKS;
    if (CUR == ':')
    {
        NEXT;
        if (CUR != ':')
        {
            xmlChar* prefix = name;
            int i;

            /*
	     * This is a namespace match
	     */
            token = xmlPatScanName(ctxt);
            for (i = 0; i < ctxt->nb_namespaces; i++)
            {
                if (xmlStrEqual(ctxt->namespaces[2 * i + 1], prefix))
                {
                    URL = xmlStrdup(ctxt->namespaces[2 * i]);
                    break;
                }
            }
            if (i >= ctxt->nb_namespaces)
            {
                ERROR5(NULL, NULL, NULL,
                       "xmlCompileStepPattern : no namespace bound to prefix %s\n",
                       prefix);
                ctxt->error = 1;
                goto error;
            }
            xmlFree(prefix);
            if (token == NULL)
            {
                if (CUR == '*')
                {
                    NEXT;
                    PUSH(XML_OP_NS, URL, NULL);
                }
                else
                {
                    ERROR(NULL, NULL, NULL,
                          "xmlCompileStepPattern : Name expected\n");
                    ctxt->error = 1;
                    goto error;
                }
            }
            else
            {
                PUSH(XML_OP_ELEM, token, URL);
            }
        }
        else
        {
            NEXT;
            if (xmlStrEqual(name, (const xmlChar*)"child"))
            {
                xmlFree(name);
                name = xmlPatScanName(ctxt);
                if (name == NULL)
                {
                    if (CUR == '*')
                    {
                        NEXT;
                        PUSH(XML_OP_ALL, NULL, NULL);
                        return;
                    }
                    else
                    {
                        ERROR(NULL, NULL, NULL,
                              "xmlCompileStepPattern : QName expected\n");
                        ctxt->error = 1;
                        goto error;
                    }
                }
                if (CUR == ':')
                {
                    xmlChar* prefix = name;
                    int i;

                    NEXT;
                    /*
		    * This is a namespace match
		    */
                    token = xmlPatScanName(ctxt);
                    for (i = 0; i < ctxt->nb_namespaces; i++)
                    {
                        if (xmlStrEqual(ctxt->namespaces[2 * i + 1], prefix))
                        {
                            URL = xmlStrdup(ctxt->namespaces[2 * i]);
                            break;
                        }
                    }
                    if (i >= ctxt->nb_namespaces)
                    {
                        ERROR5(NULL, NULL, NULL,
                               "xmlCompileStepPattern : no namespace bound to prefix %s\n",
                               prefix);
                        ctxt->error = 1;
                        goto error;
                    }
                    xmlFree(prefix);
                    if (token == NULL)
                    {
                        if (CUR == '*')
                        {
                            NEXT;
                            PUSH(XML_OP_NS, URL, NULL);
                        }
                        else
                        {
                            ERROR(NULL, NULL, NULL,
                                  "xmlCompileStepPattern : Name expected\n");
                            ctxt->error = 1;
                            goto error;
                        }
                    }
                    else
                    {
                        PUSH(XML_OP_CHILD, token, URL);
                    }
                }
                else
                    PUSH(XML_OP_CHILD, name, NULL);
                return;
            }
            else if (xmlStrEqual(name, (const xmlChar*)"attribute"))
            {
                xmlFree(name);
                name = NULL;
                xmlCompileAttributeTest(ctxt);
                if (ctxt->error != 0)
                    goto error;
                return;
            }
            else
            {
                ERROR(NULL, NULL, NULL,
                      "xmlCompileStepPattern : 'child' or 'attribute' expected\n");
                ctxt->error = 1;
                goto error;
            }
            /* NOT REACHED xmlFree(name); */
        }
    }
    else if (CUR == '*')
    {
        if (name != NULL)
        {
            ctxt->error = 1;
            goto error;
        }
        NEXT;
        PUSH(XML_OP_ALL, token, NULL);
    }
    else
    {
        if (name == NULL)
        {
            ctxt->error = 1;
            goto error;
        }
        PUSH(XML_OP_ELEM, name, NULL);
    }
    return;
error:
    if (URL != NULL)
        xmlFree(URL);
    if (token != NULL)
        xmlFree(token);
    if (name != NULL)
        xmlFree(name);
}

/**
 * xmlCompilePathPattern:
 * @ctxt:  the compilation context
 *
 * Compile the Path Pattern and generates a precompiled
 * form suitable for fast matching.
 *
 * [5]    Path    ::=    ('.//')? ( Step '/' )* ( Step | '@' NameTest ) 
 */
static void
xmlCompilePathPattern(xmlPatParserContextPtr ctxt)
{
    SKIP_BLANKS;
    if (CUR == '/')
    {
        ctxt->comp->flags |= PAT_FROM_ROOT;
    }
    else if (CUR == '.')
    {
        ctxt->comp->flags |= PAT_FROM_CUR;
    }
    if ((CUR == '/') && (NXT(1) == '/'))
    {
        PUSH(XML_OP_ANCESTOR, NULL, NULL);
        NEXT;
        NEXT;
    }
    else if ((CUR == '.') && (NXT(1) == '/') && (NXT(2) == '/'))
    {
        PUSH(XML_OP_ANCESTOR, NULL, NULL);
        NEXT;
        NEXT;
        NEXT;
    }
    if (CUR == '@')
    {
        NEXT;
        xmlCompileAttributeTest(ctxt);
        SKIP_BLANKS;
        if ((CUR != 0) || (CUR == '|'))
        {
            xmlCompileStepPattern(ctxt);
        }
    }
    else
    {
        if (CUR == '/')
        {
            PUSH(XML_OP_ROOT, NULL, NULL);
            NEXT;
        }
        xmlCompileStepPattern(ctxt);
        SKIP_BLANKS;
        while (CUR == '/')
        {
            if ((CUR == '/') && (NXT(1) == '/'))
            {
                PUSH(XML_OP_ANCESTOR, NULL, NULL);
                NEXT;
                NEXT;
                SKIP_BLANKS;
                xmlCompileStepPattern(ctxt);
            }
            else
            {
                PUSH(XML_OP_PARENT, NULL, NULL);
                NEXT;
                SKIP_BLANKS;
                if ((CUR != 0) || (CUR == '|'))
                {
                    xmlCompileStepPattern(ctxt);
                }
            }
        }
    }
    if (CUR != 0)
    {
        ERROR5(NULL, NULL, NULL,
               "Failed to compile pattern %s\n", ctxt->base);
        ctxt->error = 1;
    }
error:
    return;
}

/************************************************************************
 *									*
 *			The streaming code				*
 *									*
 ************************************************************************/

#ifdef DEBUG_STREAMING
static void
xmlDebugStreamComp(xmlStreamCompPtr stream)
{
    int i;

    if (stream == NULL)
    {
        printf("Stream: NULL\n");
        return;
    }
    printf("Stream: %d steps\n", stream->nbStep);
    for (i = 0; i < stream->nbStep; i++)
    {
        if (stream->steps[i].ns != NULL)
        {
            printf("{%s}", stream->steps[i].ns);
        }
        if (stream->steps[i].name == NULL)
        {
            printf("* ");
        }
        else
        {
            printf("%s ", stream->steps[i].name);
        }
        if (stream->steps[i].flags & XML_STREAM_STEP_ROOT)
            printf("root ");
        if (stream->steps[i].flags & XML_STREAM_STEP_DESC)
            printf("// ");
        if (stream->steps[i].flags & XML_STREAM_STEP_FINAL)
            printf("final ");
        printf("\n");
    }
}
static void
xmlDebugStreamCtxt(xmlStreamCtxtPtr ctxt, int match)
{
    int i;

    if (ctxt == NULL)
    {
        printf("Stream: NULL\n");
        return;
    }
    printf("Stream: level %d, %d states: ", ctxt->level, ctxt->nbState);
    if (match)
        printf("matches\n");
    else
        printf("\n");
    for (i = 0; i < ctxt->nbState; i++)
    {
        if (ctxt->states[2 * i] < 0)
            printf(" %d: free\n", i);
        else
        {
            printf(" %d: step %d, level %d", i, ctxt->states[2 * i],
                   ctxt->states[(2 * i) + 1]);
            if (ctxt->comp->steps[ctxt->states[2 * i]].flags &
                XML_STREAM_STEP_DESC)
                printf(" //\n");
            else
                printf("\n");
        }
    }
}
#endif
/**
 * xmlNewStreamComp:
 * @size: the number of expected steps
 *
 * build a new compiled pattern for streaming
 *
 * Returns the new structure or NULL in case of error.
 */
static xmlStreamCompPtr
xmlNewStreamComp(int size)
{
    xmlStreamCompPtr cur;

    if (size < 4)
        size = 4;

    cur = (xmlStreamCompPtr)xmlMalloc(sizeof(xmlStreamComp));
    if (cur == NULL)
    {
        ERROR(NULL, NULL, NULL,
              "xmlNewStreamComp: malloc failed\n");
        return (NULL);
    }
    memset(cur, 0, sizeof(xmlStreamComp));
    cur->steps = (xmlStreamStepPtr)xmlMalloc(size * sizeof(xmlStreamStep));
    if (cur->steps == NULL)
    {
        xmlFree(cur);
        ERROR(NULL, NULL, NULL,
              "xmlNewStreamComp: malloc failed\n");
        return (NULL);
    }
    cur->nbStep = 0;
    cur->maxStep = size;
    return (cur);
}

/**
 * xmlFreeStreamComp:
 * @comp: the compiled pattern for streaming
 *
 * Free the compiled pattern for streaming
 */
static void
xmlFreeStreamComp(xmlStreamCompPtr comp)
{
    if (comp != NULL)
    {
        if (comp->steps != NULL)
            xmlFree(comp->steps);
        if (comp->dict != NULL)
            xmlDictFree(comp->dict);
        xmlFree(comp);
    }
}

/**
 * xmlStreamCompAddStep:
 * @comp: the compiled pattern for streaming
 * @name: the first string, the name, or NULL for *
 * @ns: the second step, the namespace name
 * @flags: the flags for that step
 *
 * Add a new step to the compiled pattern
 *
 * Returns -1 in case of error or the step index if successful
 */
static int
xmlStreamCompAddStep(xmlStreamCompPtr comp, const xmlChar* name,
                     const xmlChar* ns, int flags)
{
    xmlStreamStepPtr cur;

    if (comp->nbStep >= comp->maxStep)
    {
        cur = (xmlStreamStepPtr)xmlRealloc(comp->steps,
                                           comp->maxStep * 2 * sizeof(xmlStreamStep));
        if (cur == NULL)
        {
            ERROR(NULL, NULL, NULL,
                  "xmlNewStreamComp: malloc failed\n");
            return (-1);
        }
        comp->steps = cur;
        comp->maxStep *= 2;
    }
    cur = &comp->steps[comp->nbStep++];
    cur->flags = flags;
    cur->name = name;
    cur->ns = ns;
    return (comp->nbStep - 1);
}

/**
 * xmlStreamCompile:
 * @comp: the precompiled pattern
 * 
 * Tries to stream compile a pattern
 *
 * Returns -1 in case of failure and 0 in case of success.
 */
static int
xmlStreamCompile(xmlPatternPtr comp)
{
    xmlStreamCompPtr stream;
    int i, s = 0, root = 0, flags = 0;

    if ((comp == NULL) || (comp->steps == NULL))
        return (-1);
    /*
     * special case for .
     */
    if ((comp->nbStep == 1) &&
        (comp->steps[0].op == XML_OP_ELEM) &&
        (comp->steps[0].value == NULL) &&
        (comp->steps[0].value2 == NULL))
    {
        stream = xmlNewStreamComp(0);
        if (stream == NULL)
            return (-1);
        comp->stream = stream;
        return (0);
    }

    stream = xmlNewStreamComp((comp->nbStep / 2) + 1);
    if (stream == NULL)
        return (-1);
    if (comp->dict != NULL)
    {
        stream->dict = comp->dict;
        xmlDictReference(stream->dict);
    }

    /*
     * Skip leading ./ on relative paths
     */
    i = 0;
    while ((comp->flags & PAT_FROM_CUR) && (comp->nbStep > i + 2) &&
           (comp->steps[i].op == XML_OP_ELEM) &&
           (comp->steps[i].value == NULL) &&
           (comp->steps[i].value2 == NULL) &&
           (comp->steps[i + 1].op == XML_OP_PARENT))
    {
        i += 2;
    }
    for (; i < comp->nbStep; i++)
    {
        switch (comp->steps[i].op)
        {
        case XML_OP_END:
            break;
        case XML_OP_ROOT:
            if (i != 0)
                goto error;
            root = 1;
            break;
        case XML_OP_NS:
            s = xmlStreamCompAddStep(stream, NULL,
                                     comp->steps[i].value, flags);
            flags = 0;
            if (s < 0)
                goto error;
            break;
        case XML_OP_ATTR:
            flags |= XML_STREAM_STEP_ATTR;
            s = xmlStreamCompAddStep(stream, comp->steps[i].value,
                                     comp->steps[i].value2, flags);
            flags = 0;
            if (s < 0)
                goto error;
            break;
        case XML_OP_ELEM:
            if ((comp->steps[i].value == NULL) &&
                (comp->steps[i].value2 == NULL) &&
                (comp->nbStep > i + 2) &&
                (comp->steps[i + 1].op == XML_OP_PARENT))
            {
                i++;
                continue;
            }
        case XML_OP_CHILD:
            s = xmlStreamCompAddStep(stream, comp->steps[i].value,
                                     comp->steps[i].value2, flags);
            flags = 0;
            if (s < 0)
                goto error;
            break;
        case XML_OP_ALL:
            s = xmlStreamCompAddStep(stream, NULL, NULL, flags);
            flags = 0;
            if (s < 0)
                goto error;
            break;
        case XML_OP_PARENT:
            if ((comp->nbStep > i + 1) &&
                (comp->steps[i + 1].op == XML_OP_ELEM) &&
                (comp->steps[i + 1].value == NULL) &&
                (comp->steps[i + 1].value2 == NULL))
            {
                i++;
                continue;
            }
            break;
        case XML_OP_ANCESTOR:
            flags |= XML_STREAM_STEP_DESC;
            break;
        }
    }
    stream->steps[s].flags |= XML_STREAM_STEP_FINAL;
    if (root)
        stream->steps[0].flags |= XML_STREAM_STEP_ROOT;
#ifdef DEBUG_STREAMING
    xmlDebugStreamComp(stream);
#endif
    comp->stream = stream;
    return (0);
error:
    xmlFreeStreamComp(stream);
    return (0);
}

/**
 * xmlNewStreamCtxt:
 * @size: the number of expected states
 *
 * build a new stream context
 *
 * Returns the new structure or NULL in case of error.
 */
static xmlStreamCtxtPtr
xmlNewStreamCtxt(xmlStreamCompPtr stream)
{
    xmlStreamCtxtPtr cur;

    cur = (xmlStreamCtxtPtr)xmlMalloc(sizeof(xmlStreamCtxt));
    if (cur == NULL)
    {
        ERROR(NULL, NULL, NULL,
              "xmlNewStreamCtxt: malloc failed\n");
        return (NULL);
    }
    memset(cur, 0, sizeof(xmlStreamCtxt));
    cur->states = (int*)xmlMalloc(4 * 2 * sizeof(int));
    if (cur->states == NULL)
    {
        xmlFree(cur);
        ERROR(NULL, NULL, NULL,
              "xmlNewStreamCtxt: malloc failed\n");
        return (NULL);
    }
    cur->nbState = 0;
    cur->maxState = 4;
    cur->level = 0;
    cur->comp = stream;
    return (cur);
}

/**
 * xmlFreeStreamCtxt:
 * @stream: the stream context
 *
 * Free the stream context
 */
void
xmlFreeStreamCtxt(xmlStreamCtxtPtr stream)
{
    xmlStreamCtxtPtr next;

    while (stream != NULL)
    {
        next = stream->next;
        if (stream->states != NULL)
            xmlFree(stream->states);
        xmlFree(stream);
        stream = next;
    }
}

/**
 * xmlStreamCtxtAddState:
 * @comp: the stream context
 * @idx: the step index for that streaming state
 *
 * Add a new state to the stream context
 *
 * Returns -1 in case of error or the state index if successful
 */
static int
xmlStreamCtxtAddState(xmlStreamCtxtPtr comp, int idx, int level)
{
    int i;
    for (i = 0; i < comp->nbState; i++)
    {
        if (comp->states[2 * i] < 0)
        {
            comp->states[2 * i] = idx;
            comp->states[2 * i + 1] = level;
            return (i);
        }
    }
    if (comp->nbState >= comp->maxState)
    {
        int* cur;

        cur = (int*)xmlRealloc(comp->states,
                               comp->maxState * 4 * sizeof(int));
        if (cur == NULL)
        {
            ERROR(NULL, NULL, NULL,
                  "xmlNewStreamCtxt: malloc failed\n");
            return (-1);
        }
        comp->states = cur;
        comp->maxState *= 2;
    }
    comp->states[2 * comp->nbState] = idx;
    comp->states[2 * comp->nbState++ + 1] = level;
    return (comp->nbState - 1);
}

/**
 * xmlStreamPushInternal:
 * @stream: the stream context
 * @name: the current name
 * @ns: the namespace name
 * @nodeType: the type of the node
 *
 * push new data onto the stream. NOTE: if the call xmlPatterncompile()
 * indicated a dictionnary, then strings for name and ns will be expected
 * to come from the dictionary.
 * Both @name and @ns being NULL means the / i.e. the root of the document.
 * This can also act as a reset.
 *
 * Returns: -1 in case of error, 1 if the current state in the stream is a
 *    match and 0 otherwise.
 */
static int
xmlStreamPushInternal(xmlStreamCtxtPtr stream,
                      const xmlChar* name, const xmlChar* ns,
                      xmlElementType nodeType)
{
    int ret = 0, err = 0, tmp, i, m, match, step, desc, final;
    xmlStreamCompPtr comp;
#ifdef DEBUG_STREAMING
    xmlStreamCtxtPtr orig = stream;
#endif

    if ((stream == NULL) || (stream->nbState < 0))
        return (-1);

    while (stream != NULL)
    {
        comp = stream->comp;
        if ((name == NULL) && (ns == NULL))
        {
            stream->nbState = 0;
            stream->level = 0;
            if (comp->steps[0].flags & XML_STREAM_STEP_ROOT)
            {
                tmp = xmlStreamCtxtAddState(stream, 0, 0);
                if (tmp < 0)
                    err++;
                if (comp->nbStep == 0)
                    ret = 1;
                stream = stream->next;
                continue; /* while */
            }
            stream = stream->next;
            continue; /* while */
        }

        /*
	* Fast check for ".".
	*/
        if (comp->nbStep == 0)
        {
            if (nodeType == XML_ELEMENT_NODE)
                ret = 1;
            goto stream_next;
        }

        /*
	 * Check evolution of existing states
	 */
        m = stream->nbState;
        for (i = 0; i < m; i++)
        {
            match = 0;
            step = stream->states[2 * i];
            /* dead states */
            if (step < 0)
                continue;
            /* skip new states just added */
            if (stream->states[(2 * i) + 1] > stream->level)
                continue;
            /* skip continuations */
            desc = comp->steps[step].flags & XML_STREAM_STEP_DESC;
            if ((stream->states[(2 * i) + 1] < stream->level) && (!desc))
                continue;

            /* discard old states */
            /* something needed about old level discarded */

            /* 
	    * Check for correct node-type.
	    */
            if ((comp->steps[step].flags & XML_STREAM_STEP_ATTR) &&
                (nodeType != XML_ATTRIBUTE_NODE))
                continue;

            if (comp->dict)
            {
                if (comp->steps[step].name == NULL)
                {
                    if (comp->steps[step].ns == NULL)
                        match = 1;
                    else
                        match = (comp->steps[step].ns == ns);
                }
                else
                {
                    match = ((comp->steps[step].name == name) &&
                             (comp->steps[step].ns == ns));
                }
            }
            else
            {
                if (comp->steps[step].name == NULL)
                {
                    if (comp->steps[step].ns == NULL)
                        match = 1;
                    else
                        match = xmlStrEqual(comp->steps[step].ns, ns);
                }
                else
                {
                    match = ((xmlStrEqual(comp->steps[step].name, name)) &&
                             (xmlStrEqual(comp->steps[step].ns, ns)));
                }
            }
            if (match)
            {
                final = comp->steps[step].flags & XML_STREAM_STEP_FINAL;
                if (desc)
                {
                    if (final)
                    {
                        ret = 1;
                    }
                    else
                    {
                        /* descending match create a new state */
                        xmlStreamCtxtAddState(stream, step + 1,
                                              stream->level + 1);
                    }
                }
                else
                {
                    if (final)
                    {
                        ret = 1;
                    }
                    else
                    {
                        xmlStreamCtxtAddState(stream, step + 1,
                                              stream->level + 1);
                    }
                }
            }
        }

        /*
	 * Check creating a new state.
	 */
        stream->level++;

        /*
	* Check the start only if this is a "desc" evaluation
	* or if we are at the first level of evaluation.
	*/
        desc = comp->steps[0].flags & XML_STREAM_STEP_DESC;
        if (((comp->steps[0].flags & XML_STREAM_STEP_ROOT) == 0) &&
            (((stream->flags & XML_PATTERN_NOTPATTERN) == 0) ||
             ((desc || (stream->level == 1)))
             )
            )
        {
            /*
#ifdef SUPPORT_IDC

	
	if ((desc || (stream->level == 1)) &&
	    (!(comp->steps[0].flags & XML_STREAM_STEP_ROOT))) {

	    * 
	    * Workaround for missing "self::node()" on "@foo".
	    *
	    if (comp->steps[0].flags & XML_STREAM_STEP_ATTR) {
		xmlStreamCtxtAddState(stream, 0, stream->level);
		goto stream_next;
	    }
#else
	    
	if (!(comp->steps[0].flags & XML_STREAM_STEP_ROOT)) {
#endif
	*/
            match = 0;
            if (comp->dict)
            {
                if (comp->steps[0].name == NULL)
                {
                    if (comp->steps[0].ns == NULL)
                        match = 1;
                    else
                        match = (comp->steps[0].ns == ns);
                }
                else
                {
                    if (stream->flags & XML_PATTERN_NOTPATTERN)
                    {
                        /* 
			* Workaround for missing "self::node() on "foo".
			*/
                        if (!desc)
                        {
                            xmlStreamCtxtAddState(stream, 0, stream->level);
                            goto stream_next;
                        }
                        else
                        {
                            match = ((comp->steps[0].name == name) &&
                                     (comp->steps[0].ns == ns));
                        }
                    }
                    else
                    {
                        match = ((comp->steps[0].name == name) &&
                                 (comp->steps[0].ns == ns));
                    }
                }
            }
            else
            {
                if (comp->steps[0].name == NULL)
                {
                    if (comp->steps[0].ns == NULL)
                        match = 1;
                    else
                        match = xmlStrEqual(comp->steps[0].ns, ns);
                }
                else
                {
                    if (stream->flags & XML_PATTERN_NOTPATTERN)
                    {
                        /* 
			* Workaround for missing "self::node() on "foo".
			*/
                        if (!desc)
                        {
                            xmlStreamCtxtAddState(stream, 0, stream->level);
                            goto stream_next;
                        }
                        else
                        {
                            match = ((xmlStrEqual(comp->steps[0].name, name)) &&
                                     (xmlStrEqual(comp->steps[0].ns, ns)));
                        }
                    }
                    else
                    {
                        match = ((xmlStrEqual(comp->steps[0].name, name)) &&
                                 (xmlStrEqual(comp->steps[0].ns, ns)));
                    }
                }
            }
            if (match)
            {
                if (comp->steps[0].flags & XML_STREAM_STEP_FINAL)
                    ret = 1;
                else
                    xmlStreamCtxtAddState(stream, 1, stream->level);
            }
        }
    stream_next:
        stream = stream->next;
    } /* while stream != NULL */

    if (err > 0)
        ret = -1;
#ifdef DEBUG_STREAMING
    xmlDebugStreamCtxt(orig, ret);
#endif
    return (ret);
}

/**
 * xmlStreamPush:
 * @stream: the stream context
 * @name: the current name
 * @ns: the namespace name
 *
 * push new data onto the stream. NOTE: if the call xmlPatterncompile()
 * indicated a dictionnary, then strings for name and ns will be expected
 * to come from the dictionary.
 * Both @name and @ns being NULL means the / i.e. the root of the document.
 * This can also act as a reset.
 *
 * Returns: -1 in case of error, 1 if the current state in the stream is a
 *    match and 0 otherwise.
 */
int
xmlStreamPush(xmlStreamCtxtPtr stream,
              const xmlChar* name, const xmlChar* ns)
{
    return (xmlStreamPushInternal(stream, name, ns, XML_ELEMENT_NODE));
}

/**
* xmlStreamPushAttr:
* @stream: the stream context
* @name: the current name
* @ns: the namespace name
*
* push new attribute data onto the stream. NOTE: if the call xmlPatterncompile()
* indicated a dictionnary, then strings for name and ns will be expected
* to come from the dictionary.
* Both @name and @ns being NULL means the / i.e. the root of the document.
* This can also act as a reset.
*
* Returns: -1 in case of error, 1 if the current state in the stream is a
*    match and 0 otherwise.
*/
int
xmlStreamPushAttr(xmlStreamCtxtPtr stream,
                  const xmlChar* name, const xmlChar* ns)
{
    return (xmlStreamPushInternal(stream, name, ns, XML_ATTRIBUTE_NODE));
}

/**
 * xmlStreamPop:
 * @stream: the stream context
 *
 * push one level from the stream.
 *
 * Returns: -1 in case of error, 0 otherwise.
 */
int
xmlStreamPop(xmlStreamCtxtPtr stream)
{
    int i, m;
    int ret;

    if (stream == NULL)
        return (-1);
    ret = 0;
    while (stream != NULL)
    {
        stream->level--;
        if (stream->level < 0)
            ret = -1;

        /*
	 * Check evolution of existing states
	 */
        m = stream->nbState;
        for (i = 0; i < m; i++)
        {
            if (stream->states[(2 * i)] < 0)
                break;
            /* discard obsoleted states */
            if (stream->states[(2 * i) + 1] > stream->level)
                stream->states[(2 * i)] = -1;
        }
        stream = stream->next;
    }
    return (0);
}

/************************************************************************
 *									*
 *			The public interfaces				*
 *									*
 ************************************************************************/

/**
 * xmlPatterncompile:
 * @pattern: the pattern to compile
 * @dict: an optional dictionnary for interned strings
 * @flags: compilation flags, undefined yet
 * @namespaces: the prefix definitions, array of [URI, prefix] or NULL
 *
 * Compile a pattern.
 *
 * Returns the compiled for of the pattern or NULL in case of error
 */
xmlPatternPtr
xmlPatterncompile(const xmlChar* pattern, xmlDict* dict,
                  int flags ATTRIBUTE_UNUSED,
                  const xmlChar** namespaces)
{
    xmlPatternPtr ret = NULL, cur;
    xmlPatParserContextPtr ctxt = NULL;
    const xmlChar* or, *start;
    xmlChar* tmp = NULL;
    int type = 0;
    int streamable = 1;

    if (pattern == NULL)
        return (NULL);

    start = pattern;
    or = start;
    while (* or != 0)
    {
        tmp = NULL;
        while ((* or != 0) && (* or != '|')) or ++;
        if (* or == 0)
            ctxt = xmlNewPatParserContext(start, dict, namespaces);
        else
        {
            tmp = xmlStrndup(start, or -start);
            if (tmp != NULL)
            {
                ctxt = xmlNewPatParserContext(tmp, dict, namespaces);
            }
            or ++;
        }
        if (ctxt == NULL)
            goto error;
        cur = xmlNewPattern();
        if (cur == NULL)
            goto error;
        if (ret == NULL)
            ret = cur;
        else
        {
            cur->next = ret->next;
            ret->next = cur;
        }
        cur->flags = flags;
        ctxt->comp = cur;

        xmlCompilePathPattern(ctxt);
        if (ctxt->error != 0)
            goto error;
        xmlFreePatParserContext(ctxt);

        if (streamable)
        {
            if (type == 0)
            {
                type = cur->flags & (PAT_FROM_ROOT | PAT_FROM_CUR);
            }
            else if (type == PAT_FROM_ROOT)
            {
                if (cur->flags & PAT_FROM_CUR)
                    streamable = 0;
            }
            else if (type == PAT_FROM_CUR)
            {
                if (cur->flags & PAT_FROM_ROOT)
                    streamable = 0;
            }
        }
        if (streamable)
            xmlStreamCompile(cur);
        if (xmlReversePattern(cur) < 0)
            goto error;
        if (tmp != NULL)
        {
            xmlFree(tmp);
            tmp = NULL;
        }
        start = or ;
    }
    if (streamable == 0)
    {
        cur = ret;
        while (cur != NULL)
        {
            if (cur->stream != NULL)
            {
                xmlFreeStreamComp(cur->stream);
                cur->stream = NULL;
            }
            cur = cur->next;
        }
    }

    return (ret);
error:
    if (ctxt != NULL)
        xmlFreePatParserContext(ctxt);
    if (ret != NULL)
        xmlFreePattern(ret);
    if (tmp != NULL)
        xmlFree(tmp);
    return (NULL);
}

/**
 * xmlPatternMatch:
 * @comp: the precompiled pattern
 * @node: a node
 *
 * Test wether the node matches the pattern
 *
 * Returns 1 if it matches, 0 if it doesn't and -1 in case of failure
 */
int
xmlPatternMatch(xmlPatternPtr comp, xmlNodePtr node)
{
    int ret = 0;

    if ((comp == NULL) || (node == NULL))
        return (-1);

    while (comp != NULL)
    {
        ret = xmlPatMatch(comp, node);
        if (ret != 0)
            return (ret);
        comp = comp->next;
    }
    return (ret);
}

/**
 * xmlPatternGetStreamCtxt:
 * @comp: the precompiled pattern
 *
 * Get a streaming context for that pattern
 * Use xmlFreeStreamCtxt to free the context.
 *
 * Returns a pointer to the context or NULL in case of failure
 */
xmlStreamCtxtPtr
xmlPatternGetStreamCtxt(xmlPatternPtr comp)
{
    xmlStreamCtxtPtr ret = NULL, cur;

    if ((comp == NULL) || (comp->stream == NULL))
        return (NULL);

    while (comp != NULL)
    {
        if (comp->stream == NULL)
            goto failed;
        cur = xmlNewStreamCtxt(comp->stream);
        if (cur == NULL)
            goto failed;
        if (ret == NULL)
            ret = cur;
        else
        {
            cur->next = ret->next;
            ret->next = cur;
        }
        cur->flags = comp->flags;
        comp = comp->next;
    }
    return (ret);
failed:
    xmlFreeStreamCtxt(ret);
    return (NULL);
}

/**
 * xmlPatternStreamable:
 * @comp: the precompiled pattern
 *
 * Check if the pattern is streamable i.e. xmlPatternGetStreamCtxt()
 * should work.
 *
 * Returns 1 if streamable, 0 if not and -1 in case of error.
 */
int
xmlPatternStreamable(xmlPatternPtr comp)
{
    if (comp == NULL)
        return (-1);
    while (comp != NULL)
    {
        if (comp->stream == NULL)
            return (0);
        comp = comp->next;
    }
    return (1);
}

/**
 * xmlPatternMaxDepth:
 * @comp: the precompiled pattern
 *
 * Check the maximum depth reachable by a pattern
 *
 * Returns -2 if no limit (using //), otherwise the depth,
 *         and -1 in case of error
 */
int
xmlPatternMaxDepth(xmlPatternPtr comp)
{
    int ret = 0, i;
    if (comp == NULL)
        return (-1);
    while (comp != NULL)
    {
        if (comp->stream == NULL)
            return (-1);
        for (i = 0; i < comp->stream->nbStep; i++)
            if (comp->stream->steps[i].flags & XML_STREAM_STEP_DESC)
                return (-2);
        if (comp->stream->nbStep > ret)
            ret = comp->stream->nbStep;
        comp = comp->next;
    }
    return (ret);
}

/**
 * xmlPatternFromRoot:
 * @comp: the precompiled pattern
 *
 * Check if the pattern must be looked at from the root.
 *
 * Returns 1 if true, 0 if false and -1 in case of error
 */
int
xmlPatternFromRoot(xmlPatternPtr comp)
{
    if (comp == NULL)
        return (-1);
    while (comp != NULL)
    {
        if (comp->stream == NULL)
            return (-1);
        if (comp->flags & PAT_FROM_ROOT)
            return (1);
        comp = comp->next;
    }
    return (0);
}

#define bottom_pattern
#include "elfgcchack.h"
#endif /* LIBXML_PATTERN_ENABLED */
