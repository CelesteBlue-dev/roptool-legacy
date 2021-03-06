#define BOOST_SPIRIT_USE_PHOENIX_V3

#include "Parser.h"
#include "Types.h"

#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <iomanip>
#include <algorithm>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
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
    std::string param;
} StringParam;

BOOST_FUSION_ADAPT_STRUCT(
    StringParam,
    (std::string, param)
);

typedef struct
{
    u64 param;
} ConstantParam;

BOOST_FUSION_ADAPT_STRUCT(
    ConstantParam,
    (u64, param)
)

typedef struct
{
    std::string nil;
} ReturnParam;

BOOST_FUSION_ADAPT_STRUCT(
    ReturnParam,
    (std::string, nil)
)

typedef struct
{
    u64 address;
} InlineLoadParam;

BOOST_FUSION_ADAPT_STRUCT(
    InlineLoadParam,
    (u64, address)
)

typedef boost::variant<StringParam, ConstantParam, ReturnParam, InlineLoadParam> ParameterType;

typedef struct
{
    unsigned int size;
    ParameterType param;
} Parameter;

BOOST_FUSION_ADAPT_STRUCT(
    Parameter,
    (unsigned int, size)
    (ParameterType, param)
)

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
    FunctionDataList functions;
} DataDeclImpl_;

BOOST_FUSION_ADAPT_STRUCT(
    DataDeclImpl_,
    (FunctionDataList, functions)
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
    class CreateParamaterVisitor : public boost::static_visitor<CallParameter *>
    {
        public:    
            CreateParamaterVisitor(void) { }
            
            CallParameter *operator()(StringParam str) const
            {
                StringParameter *param = new StringParameter();
                param->set(str.param);
                return param;
            }
            
            CallParameter *operator()(ConstantParam constant) const
            {
                ConstantParameter *param = new ConstantParameter();
                param->set(constant.param);
                return param;
            }
            
            CallParameter *operator()(ReturnParam constant) const
            {
                return new ReturnParameter();
            }
            
            CallParameter *operator()(InlineLoadParam inline_load) const
            {
                InlineLoadParameter *param = new InlineLoadParameter();
                param->set(inline_load.address);
                return param;
            }
    };

    RopScriptShared convertToAST(const RopScriptImpl& impl)
    {
        // create ast smart pointer
        RopScriptShared ast(new RopScript());
        DataDeclPtr data(new DataDecl);
        
        // loop through data section
        std::for_each(impl.data.functions.begin(), impl.data.functions.end(), [=, &data](const FunctionData& p)
        {
            FunctionDataDeclPtr func_data(new FunctionDataDecl);
            func_data->setName(p.name);
            func_data->setData(p.value);
            data->addFunction(func_data);
        });
        
        // add the data section
        ast->addData(data);
        
        // loop through code section
        std::for_each(impl.code.begin(), impl.code.end(), [=](const CodeData& p)
        {
            CodeDeclPtr code_data(new CodeDecl);
            code_data->setName(p.name);
            
            std::for_each(p.calls.begin(), p.calls.end(), [=, &code_data](const CallData& p)
            {
                CallDeclPtr call_data(new CallDecl);
                call_data->setName(p.name);
                
                std::for_each(p.parameters.begin(), p.parameters.end(), [=, &call_data](const Parameter& p)
                {
                    CallParameter *call_param = boost::apply_visitor(CreateParamaterVisitor(), p.param);
                    call_param->setBitlen(p.size);
                    CallParameterPtr param(call_param);
                    call_data->addParameter(param);
                    std::cout << "got param of size: " << p.size << "\n";
                });
                
                code_data->addCall(call_data);
            });
            
            ast->addCode(code_data);
        });
        
        // return the new AST
        return ast;
    }
};

//#####################################################
//
// Real Parser code
//
//#####################################################

typedef qi::symbols<char, u64> SymbolTable;

template <typename Iterator>
struct skip_grammar : qi::grammar<Iterator> 
{
    skip_grammar() : skip_grammar::base_type(skipper) 
    {
        // skip lines
        line_comment = (qi::lit("//") >> *(ascii::char_ - qi::eol) >> qi::eol);
        block_comment = qi::lit("/*") >> *(qi::char_ - "*/") >> qi::lit("*/");
        
        skipper = ascii::space | line_comment | block_comment;
    }
    
    qi::rule<Iterator> line_comment;
    qi::rule<Iterator> block_comment;
    qi::rule<Iterator> skipper;
};

template <typename Iterator>
struct error_handler 
{
    typedef qi::error_handler_result result_type;
    template<typename T3>
    qi::error_handler_result operator()(Iterator begin, Iterator last, Iterator where, T3 const& what) const
    {
        std::stringstream msg;
        const classic::file_position_base<std::string>& pos = where.get_position();

        // construct error message for the exception
        msg << pos.file << "(" << pos.line << "): '";

        std::cout << msg.str() << get_line(where, begin, last) << "' " << what << "\n";
        std::cout << std::setw(msg.str().length() + std::distance(skip_whitespace(begin), skip_whitespace(where))) << '^' << "---- here\n";
        return qi::fail;
    }
    
    Iterator skip_whitespace(Iterator begin) const
    {
        Iterator j = begin;
        while (*j == ' ' || *j == '\t' || *j == '\r' || *j == '\n') j++;
        return j;
    }
    
    std::string get_line(Iterator error_pos, Iterator begin, Iterator last) const
    {
        Iterator i = error_pos;
        
        while (i != last && (*i != '\r' && *i != '\n'))
        {
            i++;
        }
        
        return std::string(skip_whitespace(begin), i);
    }
};

template <typename Iterator>
struct ropscript_grammar : qi::grammar<Iterator, RopScriptImpl(), skip_grammar<Iterator>>
{
    SymbolTable *m_symtab;
    
    bool add_symbol(const std::string& str, u64 val)
    {
        // check map to see if symbol already exists
        if (m_symtab->find(str) != NULL)
        {
            // already exists!
            std::cout << "error: symbol: '" << str << "' already exists!\n";
            return false;
        }
        
        m_symtab->add(str, val);
        return true;
    }
    
    ropscript_grammar(SymbolTable &symtab) : ropscript_grammar::base_type(ropscript, "ropscript")
    {    
        // get a pointer to the symbol table
        m_symtab = &symtab;
        
        // top level script grammar
        ropscript = -data_section >> -code_section;
        
        // code section rules
        code_section = qi::lit("code") > -(qi::lit(':') > identifier) > '{' > *call_decl > '}';
        call_decl = identifier > '(' > -parameter_list > qi::lit(')') > qi::lit(';');
        parameter_list = param % qi::lit(',');
        param = (byte_param | word_param | dword_param | qword_param) | (qi::attr(32) > type_param);
        type_param = inline_load | string_param | expression_param | return_param;
        
        byte_type = byte | type_param;
        word_type = word | type_param;
        dword_type = dword | type_param;
        qword_type = qword | type_param;
        
        byte_param = qi::lit("BYTE") > qi::attr(8) > '(' > byte_type > ')';
        word_param = qi::lit("WORD") > qi::attr(16) > '(' > word_type > ')';
        dword_param = qi::lit("DWORD") > qi::attr(32) > '(' > dword_type > ')';
        qword_param = qi::lit("QWORD") > qi::attr(64) > '(' > qword_type > ')'; 
        
        string_param = quoted_string;
        return_param = qi::char_('R') > qi::char_('E') > qi::char_('T');
        expression_param = expression;
        //symbol_param = identifier;
        inline_load = qi::lit("LOAD") > qi::lit('[') > (expression) > qi::lit(']');
        
        // data section rules
        data_section = qi::lit("data") > '{' > *func_decl >  *symbol_decl > '}';
        func_decl = qi::lit("func") > identifier > '='> number > ';';
        symbol_decl = qi::lit("symbol") > (identifier > '=' > expression > ';')
            [qi::_pass = phx::bind(&ropscript_grammar::add_symbol, this, qi::_1, qi::_2)];
        
        // mathematical expressions
        expression = multiplicative_expr[qi::_val = qi::_1] >> 
                    *(  ('+' >> multiplicative_expr[qi::_val += qi::_1]) |
                        ('-' >> multiplicative_expr[qi::_val -= qi::_1]));
        
        multiplicative_expr = primary_expr[qi::_val = qi::_1] >>
                    *(  ('*' >> primary_expr[qi::_val *= qi::_1]) |
                        ('/' >> primary_expr[qi::_val /= qi::_1]));
                        
        primary_expr = symtab | number | ('(' > expression > ')');
       
        // numeric parsers
        number = hex32 | oct32 | dec32;
        
        // hexadecimal
        hex64 = (qi::lit("0x") | qi::lit("0X")) >> p_hex64;
        hex32 = (qi::lit("0x") | qi::lit("0X")) >> p_hex32;
        hex16 = (qi::lit("0x") | qi::lit("0X")) >> p_hex16;
        hex8 = (qi::lit("0x") | qi::lit("0X")) >> p_hex8;
        
        // decimal
        dec64 = p_dec64;
        dec32 = p_dec32;
        dec16 = p_dec16;
        dec8 = p_dec8;
        
        // octal
        oct64 = qi::lit("0") >> p_oct64;
        oct32 = qi::lit("0") >> p_oct32;
        oct16 = qi::lit("0") >> p_oct16;
        oct8 = qi::lit("0") >> p_oct8;
        
        // set individual rules
        qword = hex64 | dec64 | oct64;
        dword = hex32 | dec32 | oct32;
        word = hex16 | dec16 | oct16;
        byte = hex8 | dec8 | oct8;
        
        // string parsers
        string = qi::lexeme['"' >> +(qi::char_ - '"') >> '"'];
        quoted_string = qi::lexeme[qi::lit('"') >> +(qi::char_ - '"') >> qi::lit('"')];
       
        // define what is classed as an identifier
        // this will include function call names, variable names
        // symbol names and so forth
        identifier =  qi::lexeme[letter >> *(letter | decimal_digit)];
        
        // basic rules define what is a letter and what is a number
        letter = qi::char_("a-zA-Z_");
        decimal_digit = qi::char_("0-9");
        
        // our error handlers
        qi::on_error<qi::fail>(symbol_decl, handler(qi::_1, qi::_2, qi::_3, "Symbol redefined"));
    

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
    
    bool setDefaultWordLength(WordLength wordlen)
    {
        // we support 4 word length
        switch (wordlen)
        {
            case QWORD_LENGTH:
                number = hex64 | dec64 | oct64;
                param = (byte_param | word_param | dword_param | qword_param) | (qi::attr(64) > type_param);
                break;
                
            case DWORD_LENGTH:
                number = hex32 | dec32 | oct32;
                param = (byte_param | word_param | dword_param | qword_param) | (qi::attr(32) > type_param);
                break;
                
            case WORD_LENGTH:
                number = hex16 | dec16 | oct16;
                param = (byte_param | word_param | dword_param | qword_param) | (qi::attr(16) > type_param);
                break;
                
            case BYTE_LENGTH:
                number = hex8 | dec8 | oct8;
                param = (byte_param | word_param | dword_param | qword_param) | (qi::attr(8) > type_param);
                break;
                
            default:
                return false;
        }
        
        return true;
    }
    
    // hex
    qi::uint_parser<u64, 16, 1, 16> p_hex64;
    qi::uint_parser<u32, 16, 1, 8> p_hex32;
    qi::uint_parser<u16, 16, 1, 4> p_hex16;
    qi::uint_parser<u8, 16, 1, 2> p_hex8;
    
    // decimal
    qi::int_parser<u64, 10> p_dec64;
    qi::int_parser<u32, 10> p_dec32;
    qi::int_parser<u16, 10> p_dec16;
    qi::int_parser<u8, 10> p_dec8;
    
    // octal
    qi::uint_parser<u64, 8> p_oct64;
    qi::uint_parser<u32, 8> p_oct32;
    qi::uint_parser<u16, 8> p_oct16;
    qi::uint_parser<u8, 8> p_oct8;
    
    qi::rule<Iterator, char()> decimal_digit, letter;
    qi::rule<Iterator, u64()>     hex64, hex32, hex16, hex8, 
                                dec64, dec32, dec16, dec8,
                                oct64, oct32, oct16, oct8, number;
                                
    qi::rule<Iterator, std::string()> identifier, string, quoted_string;
    qi::rule<Iterator, FunctionData(), skip_grammar<Iterator>> func_decl;
    qi::rule<Iterator, void(), skip_grammar<Iterator>> symbol_decl;
    qi::rule<Iterator, DataDeclImpl_(), skip_grammar<Iterator>> data_section;
    
    qi::rule<Iterator, ConstantParam(), skip_grammar<Iterator>> qword, dword, word, byte;
    qi::rule<Iterator, ParameterType(), skip_grammar<Iterator>> byte_type, word_type, dword_type, qword_type;
    qi::rule<Iterator, Parameter(), skip_grammar<Iterator>> byte_param, word_param, dword_param, qword_param;
    qi::rule<Iterator, StringParam(), skip_grammar<Iterator>> string_param;
    qi::rule<Iterator, ConstantParam(), skip_grammar<Iterator>> expression_param;
    qi::rule<Iterator, ReturnParam(), skip_grammar<Iterator>> return_param;
    qi::rule<Iterator, InlineLoadParam(), skip_grammar<Iterator>> inline_load;
    qi::rule<Iterator, ParameterType(), skip_grammar<Iterator>> type_param;
    qi::rule<Iterator, Parameter(), skip_grammar<Iterator>> param;
    qi::rule<Iterator, ParameterList(), skip_grammar<Iterator>> parameter_list;
    qi::rule<Iterator, CallData(), skip_grammar<Iterator>> call_decl;
    qi::rule<Iterator, CodeData(), skip_grammar<Iterator>> code_section;
    qi::rule<Iterator, RopScriptImpl(), skip_grammar<Iterator>> ropscript;
    
    
    qi::rule<Iterator, u64(), skip_grammar<Iterator>> primary_expr;
    qi::rule<Iterator, u64(), skip_grammar<Iterator>> multiplicative_expr;
    qi::rule<Iterator, u64(), skip_grammar<Iterator>> additive_expr;
    qi::rule<Iterator, u64(), skip_grammar<Iterator>> expression;
    
    // error handler
    phx::function<error_handler<Iterator>> handler;
};

RopScriptShared parse(const char *filename, WordLength bitlen)
{
    std::ifstream ifs;
    RopScriptImpl out;
    SymbolTable symtab;
    
    // open file
    ifs.open(filename, std::ios_base::in);
    
    // check if open failure
    if (!ifs)
    {
        std::stringstream msg;
        msg << "Error: Could not open input file: " << filename;
        throw std::runtime_error(msg.str());;
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
    pos_iterator_type iter = position_begin;
    pos_iterator_type position_end;
  
    ropscript_grammar<pos_iterator_type> parser(symtab);
    parser.setDefaultWordLength(bitlen);
    
    bool r = false;
    
    try
    {
        // parse the script file
        r = qi::phrase_parse(iter, position_end, parser, skip_grammar<pos_iterator_type>(), out);
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
    return convertToAST(out);
}
