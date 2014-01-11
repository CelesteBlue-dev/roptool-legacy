#ifndef _COMPILER_H_
#define _COMPILER_H_

#include "AST.h"
#include "Target.h"

// std
#include <memory>
#include <string>

class Compiler : public ASTVisitor
{
	public:
		// CTOR
		Compiler(void) { }
		
		// compile
		void compile(VisitablePtr m_ast, TargetPtr m_target);
		
		// visitor methods
        void visit(StringParameter *param);
        void visit(ConstantParameter *param);
        void visit(ReturnParameter *param);
        void visit(InlineLoadParameter *param);
        void visit_enter(CallDecl *param);
        void visit_exit(CallDecl *param);
        void visit_enter(CodeDecl *param);
        void visit_exit(CodeDecl *param);
        void visit(FunctionDataDecl *param);
        void visit(DataDecl *param);
        void visit_enter(RopScript *param);
        void visit_exit(RopScript *param);

	private:
		VisitablePtr m_ast;
		TargetPtr m_target;
		
		std::map<std::string, Function> m_functions;
		
		// this stores the function call param types
		std::vector<char> m_param_type;
		
		// this stores the function call data references
		std::vector<u64> m_param;
};

#endif // _COMPILER_H_
