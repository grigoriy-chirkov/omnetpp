//==========================================================================
//  CPPEXPRGENERATOR.H - part of
//
//                     OMNeT++/OMNEST
//            Discrete System Simulation in C++
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 2002-2004 Andras Varga

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __CPPEXPRGENERATOR_H
#define __CPPEXPRGENERATOR_H

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include "nedelements.h"

using std::ostream;
class NEDSymbolTable;


/**
 * Helper class for NEDCppGenerator. Should be used in the following manner:
 *
 * 1. before entering a module declaration, collectExpressions() and
 *    generateExpressionClasses() should be called
 *
 * 2. inside the module, where expressions are actually used,
 *    generateExpressionUsage() should be called after outputting
 *    text like "value="
 *
 * The internal needsExpressionClass() method decides which expressions
 * are compiled inline and which with expression classes.
 *
 * @ingroup CppGenerator
 */
class CppExpressionGenerator
{
  protected:
    typedef std::vector<NEDElement *> NEDElementVector;
    struct ExpressionInfo {
        ExpressionNode *expr;
        int ctxtype; // tagcode of toplevel element which contains this expr.
        NEDElement *submoduleTypeDecl; // if submodule or network, the type decl.
        std::string name; // generated expression class name (Expr0, Expr1, etc)
        NEDElementVector ctorargs;
        NEDElementVector cachedvars;
    };
    typedef std::map<ExpressionNode *,ExpressionInfo> NEDExpressionMap;

    static int count;
    ostream& out;
    NEDSymbolTable *symboltable;
    NEDExpressionMap exprMap;

    enum {
      MODE_INLINE_EXPRESSION,
      MODE_EXPRESSION_CLASS,
    };

    void doExtractArgs(ExpressionInfo& info, NEDElement *node);
    void doCollectExpressions(NEDElement *node, NEDElement *currentSubmodTypeDecl);
    void collectExpressionInfo(ExpressionNode *expr, NEDElement *currentSubmodTypeDecl);
    void generateExpressionClass(ExpressionInfo& info);
    const char *getTypeForArg(NEDElement *node);
    const char *getNameForArg(NEDElement *node);
    void doValueForArg(NEDElement *node);
    void doValueForCachedVar(NEDElement *node);

    void generateChildren(NEDElement *node, const char *indent, int mode);
    void generateItem(NEDElement *node, const char *indent, int mode);
    void doOperator(OperatorNode *node, const char *indent, int mode);
    void doFunction(FunctionNode *node, const char *indent, int mode);
    void doParamref(ParamRefNode *node, const char *indent, int mode);
    void doIdent(IdentNode *node, const char *indent, int mode);
    void doConst(ConstNode *node, const char *indent, int mode);
    void doExpression(ExpressionNode *node, const char *indent, int mode);

    bool needsExpressionClass(ExpressionNode *expr, NEDElement *currentSubmodTypeDecl);

  public:
    /**
     * Constructor.
     */
    CppExpressionGenerator(ostream& out, NEDSymbolTable *symboltable);

    /**
     * Destructor.
     */
    ~CppExpressionGenerator() {}

    /**
     * Recursively finds all &lt;expression&gt; tags within the passed node, and
     * stores references to them.
     */
    void collectExpressions(NEDElement *node);

    /**
     * Generate compiled expression classes (those derived from cExpression)
     * from all stored expressions. The evaluate() methods of the generated
     * classes will hold the expressions.
     */
    void generateExpressionClasses();

    /**
     * Generate a cPar initialization code that uses the compiled expression
     * classes generated by generateExpressionClasses().
     */
    void generateExpressionUsage(ExpressionNode *expr, const char *indent);
};

#endif


