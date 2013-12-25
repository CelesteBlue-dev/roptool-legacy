#include "Parser.h"

#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <iomanip>
#include <algorithm>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_fusion.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/phoenix_object.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/std_pair.hpp>

#include <boost/spirit/include/support_multi_pass.hpp>
#include <boost/spirit/include/classic_position_iterator.hpp>

namespace classic = boost::spirit::classic;

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
namespace phx = boost::phoenix;

typedef struct
{
	std::string name;
	Function value;
} FunctionData;

BOOST_FUSION_ADAPT_STRUCT(
	FunctionData,
	(std::string, name)
	(Function, value)
);

typedef std::vector<FunctionData> FunctionDataList;

typedef struct
{
	std::string name;
	Symbol value;
} SymbolData;

BOOST_FUSION_ADAPT_STRUCT(
	SymbolData,
	(std::string, name)
	(Symbol, value)
);

typedef std::vector<SymbolData> SymbolDataList;

typedef struct
{
	FunctionDataList functions;
	SymbolDataList symbols;
} DataDeclImpl_;

BOOST_FUSION_ADAPT_STRUCT(
	DataDeclImpl_,
	(FunctionDataList, functions)
	(SymbolDataList, symbols)
);

typedef std::vector<Parameter> ParameterList;

typedef struct
{
	std::string name;
	ParameterList parameters;
} CallData;

BOOST_FUSION_ADAPT_STRUCT(
	CallData,
	(std::string, name)
	(ParameterList, parameters)
);

typedef std::vector<CallData> CallDataList;

typedef struct
{
	std::string name;
	CallDataList calls;
} CodeData;

BOOST_FUSION_ADAPT_STRUCT(
	CodeData,
	(std::string, name)
	(CallDataList, calls)
);

typedef std::vector<CodeData> CodeDataList;

typedef struct
{
	DataDeclImpl_ data;
	CodeDataList code;
} RopScriptImpl;

BOOST_FUSION_ADAPT_STRUCT(
	RopScriptImpl,
	(DataDeclImpl_, data)
	(CodeDataList, code)
);
	
namespace
{
	RopScriptShared convertToAST(const RopScriptImpl& impl)
	{
		// create ast smart pointer
		RopScriptShared ast(new RopScript());
		DataDecl data;
		
		// loop through data section
		std::for_each(impl.data.functions.begin(), impl.data.functions.end(), [=, &data](const FunctionData& p)
		{
			FunctionDataDecl func_data;
			func_data.setName(p.name);
			func_data.setData(p.value);
			data.addFunction(func_data);
		});
		
		// loop through symbols
		std::for_each(impl.data.symbols.begin(), impl.data.symbols.end(), [=, &data](const SymbolData& p)
		{
			SymbolDataDecl symbol_data;
			symbol_data.setName(p.name);
			symbol_data.setData(p.value);
			data.addSymbol(symbol_data);
		});
		
		// add the data section
		ast->addData(data);
		
		// loop through code section
		std::for_each(impl.code.begin(), impl.code.end(), [=](const CodeData& p)
		{
			CodeDecl code_data;
			code_data.setName(p.name);
			
			std::for_each(p.calls.begin(), p.calls.end(), [=, &code_data](const CallData& p)
			{
				CallDecl call_data;
				call_data.setName(p.name);
				
				std::for_each(p.parameters.begin(), p.parameters.end(), [=, &call_data](const Parameter& p)
				{
					CallParameter call_param;
					call_param.setParameter(p);
					call_data.addParameter(call_param);
				});
				
				code_data.addCall(call_data);
			});
			
			ast->addCode(code_data);
		});
		
		// return the new AST
		return ast;
	}
};

template <typename Iterator>
struct skip_grammar : qi::grammar<Iterator> 
{
    skip_grammar() : skip_grammar::base_type(skipper) 
    {
        // skip lines
        line_comment = (qi::lit("//") >> *(ascii::char_ - qi::eol) >> qi::eol);
        block_comment = (qi::lit("/*") >> *(ascii::char_ - '*') >> +qi::lit('*') >> '/');
        skipper = ascii::space | line_comment | block_comment;
    }
    
    qi::rule<Iterator> line_comment;
    qi::rule<Iterator> block_comment;
    qi::rule<Iterator> skipper;
};

template <typename Iterator>
struct ropscript_grammar : qi::grammar<Iterator, RopScriptImpl(), skip_grammar<Iterator>>
{
    ropscript_grammar() : ropscript_grammar::base_type(ropscript, "ropscript")
    {
        // top level script grammar
        ropscript = -data_section >> -code_section;
        
        // code section rules
        code_section = qi::lit("code") > -(qi::lit(':') > identifier) > '{' > *call_decl > '}';
		call_decl = identifier > '(' > -parameter_list > qi::lit(')') > qi::lit(';');
        parameter_list = param % qi::lit(',');
        param = inline_load | quoted_string | expression | identifier;
        inline_load = qi::lexeme[qi::lit("LOAD") > qi::char_('[') > +(qi::char_ - ']') > qi::char_(']')];
		
        // data section rules
        data_section = qi::lit("data") > '{' > *func_decl >  *symbol_decl > '}';
        func_decl = qi::lit("func") > identifier > '='> number > ';';
        symbol_decl = qi::lit("symbol") > identifier > '=' > (number | string)  > ';';

        // mathematical expressions
        expression = multiplicative_expr[qi::_val = qi::_1] >> 
                    *(  ('+' >> multiplicative_expr[qi::_val += qi::_1]) |
                        ('-' >> multiplicative_expr[qi::_val -= qi::_1]));
        
        multiplicative_expr = primary_expr[qi::_val = qi::_1] >>
                    *(  ('*' >> primary_expr[qi::_val *= qi::_1]) |
                        ('/' >> primary_expr[qi::_val /= qi::_1]));
                        
        primary_expr = number | ('(' > expression > ')');
        
        
        // define what is classed as an identifier
        // this will include function call names, variable names
        // symbol names and so forth
        identifier =  qi::lexeme[letter >> *(letter | decimal_digit)];
        decimal = qi::int_;
        hex = (qi::lit("0x") | qi::lit("0X")) >> qi::hex;
        octal = qi::lit("0") >> qi::oct;
        number = hex | octal | decimal;
        string = qi::lexeme['"' >> +(qi::char_ - '"') >> '"'];
        quoted_string = qi::lexeme[qi::char_('"') >> +(qi::char_ - '"') >> qi::char_('"')];
        
        // basic rules define what is a letter and what is a number
        letter = qi::char_("a-zA-Z_");
        decimal_digit = qi::char_("0-9");
        
        // name the rules
        /*ropscript.name("ropscript");
        code_section.name("code_section");
        call_decl.name("call_decl");
        parameter_list.name("parameter_list");
        param.name("param");
        data_section.name("data_section");
        symbol_decl.name("symbol_decl");
        func_decl.name("func_decl");
        identifier.name("identifier");
        string.name("string");
        quoted_string.name("quoted_string");
        number.name("number");
        letter.name("letter");
        decimal_digit.name("decimal_digit");*/
    }
    
    qi::rule<Iterator, char()> decimal_digit, letter;
    qi::rule<Iterator, int()> decimal, octal, hex, number;
    qi::rule<Iterator, std::string()> identifier, string, quoted_string;
    qi::rule<Iterator, FunctionData(), skip_grammar<Iterator>> func_decl;
    qi::rule<Iterator, SymbolData(), skip_grammar<Iterator>> symbol_decl;
    qi::rule<Iterator, DataDeclImpl_(), skip_grammar<Iterator>> data_section;
    
    qi::rule<Iterator, Parameter(), skip_grammar<Iterator>> param;
    qi::rule<Iterator, ParameterList(), skip_grammar<Iterator>> parameter_list;
    qi::rule<Iterator, CallData(), skip_grammar<Iterator>> call_decl;
    qi::rule<Iterator, CodeData(), skip_grammar<Iterator>> code_section;
    qi::rule<Iterator, RopScriptImpl(), skip_grammar<Iterator>> ropscript;
    qi::rule<Iterator, std::string(), skip_grammar<Iterator>> inline_load;
    
    
    qi::rule<Iterator, int(), skip_grammar<Iterator>> primary_expr;
    qi::rule<Iterator, int(), skip_grammar<Iterator>> multiplicative_expr;
    qi::rule<Iterator, int(), skip_grammar<Iterator>> additive_expr;
    qi::rule<Iterator, int(), skip_grammar<Iterator>> expression;
};

RopScriptShared parse(const char *filename)
{
    std::ifstream ifs;
	RopScriptImpl out;
    
    // open file
    ifs.open(filename, std::ios_base::in);
    
    // check if open failure
    if (!ifs)
    {
        std::cerr << "Error: Could not open input file: " << filename << std::endl;
        return false;
    }
        
    typedef std::istreambuf_iterator<char> base_iterator_type;
    base_iterator_type in_begin(ifs);

    // convert input iterator to forward iterator, usable by spirit parser
    typedef boost::spirit::multi_pass<base_iterator_type> forward_iterator_type;
    forward_iterator_type fwd_begin = boost::spirit::make_default_multi_pass(in_begin);
    forward_iterator_type fwd_end;

    // wrap forward iterator with position iterator, to record the position
    typedef classic::position_iterator2<forward_iterator_type> pos_iterator_type;
    pos_iterator_type position_begin(fwd_begin, fwd_end, filename);
    pos_iterator_type position_end;
  
    ropscript_grammar<pos_iterator_type> parser;
    bool r = false;
    
    try
    {
        // parse the script file
        r = qi::phrase_parse(position_begin, position_end, parser, skip_grammar<pos_iterator_type>(), out);
    }

    // catch input expectation failure
    catch (const qi::expectation_failure<pos_iterator_type>& e)
    {
        std::stringstream msg;
        std::string got = std::string(e.first, e.last);
        //std::getline(e.first, got);
        
        // get the position of the iterator relative to the file
        const classic::file_position_base<std::string>& pos = e.first.get_position();
       
        // construct error message for the exception
        msg << pos.file << "(" << pos.line << "): " << "expected: " << e.what_ << " got:" << got;
        // throw exception
        throw std::runtime_error(msg.str());
    }
    
    if (r == false)
    {
        throw std::runtime_error("problem parsing file");
    }

	// convert RopTool output
	RopScriptShared script = convertToAST(out);
	
    //std::cout << "data:\n";
    /*std::for_each(out->data.functions.begin(), out->data.functions.end(), [=](const func_pair& p)
    {
        std::cout << "\tfunction: " << p.first << " address: " << p.second << "\n";
    });
 
    std::for_each(out->data.symbols.begin(), out->data.symbols.end(), [=](const symbol_pair& p)
    {
        std::cout << "\tsymbol: " << p.first << " value: " << p.second << "\n";
    });
    
    std::cout << "code: " << out->code.name << "\n";
    std::for_each(out->code.call_list.begin(), out->code.call_list.end(), [=](const CallDecl& p)
    {
        std::cout << "\tcall: " << p.callee << "\n";
        std::cout << "\tparams: " << p.parameter_list.size() << "\n";
        std::for_each(p.parameter_list.begin(), p.parameter_list.end(), [=](const parameter& p)
        {
            std::cout << "\t\tparam: " << p << "\n";
        });
    });*/
    
    return script;
}
