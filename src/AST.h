#ifndef _AST_H_
#define _AST_H_

// std
#include <string>
#include <memory>
#include <vector>
#include <map>

// boost
#include <boost/variant.hpp>

// forward declaration of the classes in the AST
class SymbolParameter;
class StringParameter;
class ConstantParameter;
class ReturnParameter;
class InlineLoadParameter;
class CallParameter;
class CallDecl;
class CodeDecl;
class DataDecl;
class RopScript;

template<typename T>
class DataDeclImpl;

typedef unsigned int Function;
typedef boost::variant<unsigned int, std::string> Symbol;


typedef DataDeclImpl<Function> FunctionDataDecl;
typedef DataDeclImpl<Symbol> SymbolDataDecl;

class ASTVisitor
{
    public:
        virtual void visit(SymbolParameter *param) = 0;
        virtual void visit(StringParameter *param) = 0;
        virtual void visit(ConstantParameter *param) = 0;
        virtual void visit(ReturnParameter *param) = 0;
        virtual void visit(InlineLoadParameter *param) = 0;
        virtual void visit_enter(CallDecl *param) = 0;
        virtual void visit_exit(CallDecl *param) = 0;
        virtual void visit_enter(CodeDecl *param) = 0;
        virtual void visit_exit(CodeDecl *param) = 0;
        virtual void visit(FunctionDataDecl *param) = 0;
        virtual void visit(SymbolDataDecl *param) = 0;
        virtual void visit(DataDecl *param) = 0;
        virtual void visit_enter(RopScript *param) = 0;
        virtual void visit_exit(RopScript *param) = 0;
};

class ASTVisitable
{
    public:
        virtual void traverse(ASTVisitor *visitor) = 0;
};

class CallParameter : public ASTVisitable
{
    public:		
        void traverse(ASTVisitor *visitor) = 0;
		
		enum Type
		{
			CONSTANT = 0,
			SYMBOL,
			STRING,
			RETURN,
			INLINE_LOAD,
		};
		
		virtual Type type(void) const = 0;
};

typedef std::shared_ptr<CallParameter> CallParameterPtr;
typedef std::vector<CallParameterPtr> CallParameterPtrList;

class SymbolParameter : public CallParameter
{
	public:
		SymbolParameter(void) { }
		void traverse(ASTVisitor *visitor);
		
		void set(const std::string& symbol) { m_symbol = symbol; }
		const std::string& value(void) const { return m_symbol; }
		
		CallParameter::Type type(void) const { return CallParameter::SYMBOL; }
	
	private:
		std::string m_symbol;
		
};

class StringParameter : public CallParameter
{
	public:
		StringParameter(void) { }
		void traverse(ASTVisitor *visitor);
		
		void set(const std::string& str) { m_str = str; }
		const std::string& value(void) const { return m_str; }
		
		CallParameter::Type type(void) const { return CallParameter::STRING; }
	
	private:
		std::string m_str;
		
};

class ConstantParameter : public CallParameter
{
	public:
		ConstantParameter(void) { }
		void traverse(ASTVisitor *visitor);
		
		void set(unsigned int val) { m_val = val; }
		unsigned int value(void) const { return m_val; }
		
		CallParameter::Type type(void) const { return CallParameter::CONSTANT; }
	
	private:
		unsigned int m_val;
		
};

class ReturnParameter : public CallParameter
{
	public:
		ReturnParameter(void) { }
		void traverse(ASTVisitor *visitor);
		
		unsigned int value(void) const { return 0; }
		CallParameter::Type type(void) const { return CallParameter::RETURN; }
};

class InlineLoadParameter : public CallParameter
{
	public:
		InlineLoadParameter(void) { }
		void traverse(ASTVisitor *visitor);
		
		void set(unsigned int address) { m_address = address; }
		unsigned int value(void) const { return m_address; }
		
		CallParameter::Type type(void) const { return CallParameter::INLINE_LOAD; }
	
	private:
		unsigned int m_address;
};

class CallDecl : public ASTVisitable
{
    public:
        std::string& name(void) { return m_name; }
        const std::string& name(void) const { return m_name; }
        void setName(const std::string& name) { m_name = name; }
        
        CallParameterPtrList& parameters(void) { return m_params; }
        const CallParameterPtrList& parameters(void) const { return m_params; }
        void addParameter(CallParameterPtr param) { m_params.push_back(param); }
        void addParameter(CallParameterPtrList list) { m_params.insert(m_params.end(), list.begin(), list.end()); }
        void traverse(ASTVisitor *visitor);
    
    private:
        CallParameterPtrList m_params;
        std::string m_name;
};

typedef std::shared_ptr<CallDecl> CallDeclPtr;
typedef std::vector<CallDeclPtr> CallDeclPtrList;

class CodeDecl : public ASTVisitable
{
    public:
        const std::string& name(void) const { return m_name; }
        void setName(const std::string& name) { m_name = name; }
        
        const CallDeclPtrList& calls(void) const { return m_calls; }
        void addCall(CallDeclPtr call) { m_calls.push_back(call); }
        
        void traverse(ASTVisitor *visitor);
        
    private:
        std::string m_name;
        CallDeclPtrList m_calls;
};

template<typename T>
class DataDeclImpl : public ASTVisitable
{
    public:
        const std::string& name(void) const { return m_name; }
        void setName(std::string name) { m_name = name; }
        
        const T& getData(void) const { return m_data; }
        void setData(const T& data) { m_data = data; }
        
        void traverse(ASTVisitor *visitor);
        
    private:
        std::string m_name;
        T m_data;
};

typedef std::shared_ptr<FunctionDataDecl> FunctionDataDeclPtr;
typedef std::vector<FunctionDataDeclPtr> FunctionDataDeclPtrList;

typedef std::shared_ptr<SymbolDataDecl> SymbolDataDeclPtr;
typedef std::vector<SymbolDataDeclPtr> SymbolDataDeclPtrList;

class DataDecl : public ASTVisitable
{
    public:
        const FunctionDataDeclPtrList& functions(void) const { return m_func_data; } 
        void addFunction(FunctionDataDeclPtr func_data) { m_func_data.push_back(func_data); }
        
        const SymbolDataDeclPtrList& symbols(void) const { return m_sym_data; }
        void addSymbol(SymbolDataDeclPtr symbol_data) { m_sym_data.push_back(symbol_data); }
        
        void traverse(ASTVisitor *visitor);
        
    private:
        SymbolDataDeclPtrList m_sym_data;
        FunctionDataDeclPtrList m_func_data;
};

typedef std::shared_ptr<DataDecl> DataDeclPtr;
typedef std::vector<DataDeclPtr> DataDeclPtrList;

typedef std::shared_ptr<CodeDecl> CodeDeclPtr;
typedef std::vector<CodeDeclPtr> CodeDeclPtrList;

class RopScript : public ASTVisitable
{
    public:
        const DataDeclPtrList& data(void) const { return m_data; }
        void addData(DataDeclPtr data) { m_data.push_back(data); }
        
        const CodeDeclPtrList& code(void) const { return m_code; }
        void addCode(CodeDeclPtr code) { m_code.push_back(code); }
        
        void traverse(ASTVisitor *visitor);
        
    private:
        DataDeclPtrList m_data;
        CodeDeclPtrList m_code;
};

typedef std::shared_ptr<RopScript> RopScriptShared;
typedef std::shared_ptr<ASTVisitable> VisitablePtr;

#endif //_AST_H_