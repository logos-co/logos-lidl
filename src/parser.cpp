#include "lidl/parser.hpp"
#include "lidl/lexer.hpp"

#include <unordered_set>

namespace lidl {

namespace {

const std::unordered_set<std::string>& builtinTypes()
{
    static const std::unordered_set<std::string> bt = {
        "tstr", "bstr", "int", "uint", "float64", "bool", "result", "any"
    };
    return bt;
}

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : m_tokens(tokens) {}

    ParseResult parse() {
        ParseResult result;
        if (!parseModule(result.module)) {
            result.error = m_error; result.errorLine = m_errorLine; result.errorColumn = m_errorColumn;
        }
        return result;
    }

private:
    const std::vector<Token>& m_tokens;
    size_t m_pos = 0;
    std::string m_error;
    int m_errorLine = 0, m_errorColumn = 0;

    const Token& current() const {
        return m_tokens[m_pos < m_tokens.size() ? m_pos : m_tokens.size() - 1];
    }
    bool at(Token::Type type) const { return current().type == type; }
    bool consume(Token::Type type) { if (!at(type)) return false; ++m_pos; return true; }

    // The LIDL keywords are reserved only *structurally* — at the start of a
    // declaration. In a *name* position they are ordinary identifiers (an
    // event parameter named `version` must round-trip). The grammar stays
    // unambiguous because every name position is reached only after a
    // structural token has already been consumed.
    bool atName() const {
        switch (current().type) {
        case Token::Ident:
        case Token::Module: case Token::TypeKw: case Token::Method:
        case Token::Event: case Token::Version: case Token::Description:
        case Token::Category: case Token::Depends:
            return true;
        default:
            return false;
        }
    }

    bool expect(Token::Type type, const std::string& context) {
        if (consume(type)) return true;
        error("Expected " + tokenTypeName(type) + " in " + context + ", got '" + current().text + "'");
        return false;
    }

    void error(const std::string& msg) {
        if (!m_error.empty()) return;
        m_error = msg; m_errorLine = current().line; m_errorColumn = current().column;
    }

    static std::string tokenTypeName(Token::Type t) {
        switch (t) {
        case Token::Module: return "'module'"; case Token::TypeKw: return "'type'";
        case Token::Method: return "'method'"; case Token::Event: return "'event'";
        case Token::Version: return "'version'"; case Token::Description: return "'description'";
        case Token::Category: return "'category'"; case Token::Depends: return "'depends'";
        case Token::Ident: return "identifier"; case Token::StringLit: return "string literal";
        case Token::LBrace: return "'{'"; case Token::RBrace: return "'}'";
        case Token::LParen: return "'('"; case Token::RParen: return "')'";
        case Token::LBracket: return "'['"; case Token::RBracket: return "']'";
        case Token::Colon: return "':'"; case Token::Comma: return "','";
        case Token::Arrow: return "'->'"; case Token::Question: return "'?'";
        case Token::Eof: return "end of input"; case Token::Error: return "error";
        }
        return "unknown";
    }

    bool parseModule(ModuleDecl& mod) {
        if (!expect(Token::Module, "module declaration")) return false;
        if (!atName()) { error("Expected module name"); return false; }
        mod.name = current().text; ++m_pos;
        if (!expect(Token::LBrace, "module declaration")) return false;
        if (!parseModuleBody(mod)) return false;
        if (!expect(Token::RBrace, "module declaration")) return false;
        if (!at(Token::Eof)) { error("Unexpected content after module closing '}'"); return false; }
        return true;
    }

    bool parseModuleBody(ModuleDecl& mod) {
        while (!at(Token::RBrace) && !at(Token::Eof)) {
            switch (current().type) {
            case Token::Version: case Token::Description: case Token::Category: case Token::Depends:
                if (!parseMetadata(mod)) return false; break;
            case Token::TypeKw: if (!parseTypeDef(mod)) return false; break;
            case Token::Method: if (!parseMethodDef(mod)) return false; break;
            case Token::Event: if (!parseEventDef(mod)) return false; break;
            default: error("Unexpected token '" + current().text + "' in module body"); return false;
            }
        }
        return true;
    }

    bool parseMetadata(ModuleDecl& mod) {
        if (at(Token::Version)) { ++m_pos; if (!at(Token::StringLit)) { error("Expected string after 'version'"); return false; } mod.version = current().text; ++m_pos; return true; }
        if (at(Token::Description)) { ++m_pos; if (!at(Token::StringLit)) { error("Expected string after 'description'"); return false; } mod.description = current().text; ++m_pos; return true; }
        if (at(Token::Category)) { ++m_pos; if (!at(Token::StringLit)) { error("Expected string after 'category'"); return false; } mod.category = current().text; ++m_pos; return true; }
        if (at(Token::Depends)) {
            ++m_pos;
            if (!expect(Token::LBracket, "depends list")) return false;
            if (!at(Token::RBracket)) {
                if (!atName()) { error("Expected identifier in depends list"); return false; }
                mod.depends.push_back(current().text); ++m_pos;
                while (consume(Token::Comma)) {
                    if (!atName()) { error("Expected identifier after ',' in depends list"); return false; }
                    mod.depends.push_back(current().text); ++m_pos;
                }
            }
            if (!expect(Token::RBracket, "depends list")) return false;
            return true;
        }
        error("Expected metadata keyword"); return false;
    }

    bool parseTypeDef(ModuleDecl& mod) {
        if (!expect(Token::TypeKw, "type definition")) return false;
        TypeDecl td;
        if (!atName()) { error("Expected type name"); return false; }
        td.name = current().text; ++m_pos;
        if (!expect(Token::LBrace, "type definition")) return false;
        while (!at(Token::RBrace) && !at(Token::Eof)) { FieldDecl fd; if (!parseFieldDef(fd)) return false; td.fields.push_back(fd); }
        if (!expect(Token::RBrace, "type definition")) return false;
        mod.types.push_back(td); return true;
    }

    bool parseFieldDef(FieldDecl& fd) {
        fd.optional = consume(Token::Question);
        if (!atName()) { error("Expected field name"); return false; }
        fd.name = current().text; ++m_pos;
        if (!expect(Token::Colon, "field definition")) return false;
        return parseTypeExpr(fd.type);
    }

    bool parseMethodDef(ModuleDecl& mod) {
        if (!expect(Token::Method, "method definition")) return false;
        MethodDecl md;
        if (!atName()) { error("Expected method name"); return false; }
        md.name = current().text; ++m_pos;
        if (!expect(Token::LParen, "method parameters")) return false;
        if (!parseParams(md.params)) return false;
        if (!expect(Token::RParen, "method parameters")) return false;
        if (!expect(Token::Arrow, "method return type")) return false;
        if (!parseTypeExpr(md.returnType)) return false;
        // Optional trailing doc: `-> ret description "..."`. Carries the
        // method's doc comment across a .lidl round-trip so introspection
        // (lm / getMethods) still surfaces it.
        if (at(Token::Description)) {
            ++m_pos;
            if (!at(Token::StringLit)) { error("Expected string after method 'description'"); return false; }
            md.description = current().text; ++m_pos;
        }
        // Restore the return-shape flags from the parsed type so a .lidl
        // round-trip carries the same semantics the impl-header parser sets
        // (it derives them from C++ types: StdLogosResult -> result,
        // LogosMap/LogosList -> json). Without this, a header-first universal
        // module (header -> .lidl -> cdylib backend) loses the flags and the
        // cdylib codegen/eligibility mis-handles result / map / list returns.
        {
            const TypeExpr& rt = md.returnType;
            md.resultReturn = (rt.kind == TypeExpr::Primitive && rt.name == "result");
            md.jsonReturn =
                rt.kind == TypeExpr::Map
                || (rt.kind == TypeExpr::Primitive && rt.name == "any")
                || (rt.kind == TypeExpr::Array && rt.elements.size() == 1
                    && rt.elements[0].kind == TypeExpr::Primitive
                    && rt.elements[0].name == "any");
        }
        mod.methods.push_back(md); return true;
    }

    bool parseEventDef(ModuleDecl& mod) {
        if (!expect(Token::Event, "event definition")) return false;
        EventDecl ed;
        if (!atName()) { error("Expected event name"); return false; }
        ed.name = current().text; ++m_pos;
        if (!expect(Token::LParen, "event parameters")) return false;
        if (!parseParams(ed.params)) return false;
        if (!expect(Token::RParen, "event parameters")) return false;
        if (at(Token::Description)) {
            ++m_pos;
            if (!at(Token::StringLit)) { error("Expected string after event 'description'"); return false; }
            ed.description = current().text; ++m_pos;
        }
        mod.events.push_back(ed); return true;
    }

    bool parseParams(std::vector<ParamDecl>& params) {
        if (at(Token::RParen)) return true;
        ParamDecl p; if (!parseParam(p)) return false; params.push_back(p);
        while (consume(Token::Comma)) { ParamDecl p2; if (!parseParam(p2)) return false; params.push_back(p2); }
        return true;
    }

    bool parseParam(ParamDecl& p) {
        if (!atName()) { error("Expected parameter name"); return false; }
        p.name = current().text; ++m_pos;
        if (!expect(Token::Colon, "parameter")) return false;
        return parseTypeExpr(p.type);
    }

    bool parseTypeExpr(TypeExpr& te) {
        if (consume(Token::Question)) { te.kind = TypeExpr::Optional; te.elements.resize(1); return parseTypeExpr(te.elements[0]); }
        if (consume(Token::LBracket)) { te.kind = TypeExpr::Array; te.elements.resize(1); if (!parseTypeExpr(te.elements[0])) return false; return expect(Token::RBracket, "array type"); }
        if (consume(Token::LBrace)) { te.kind = TypeExpr::Map; te.elements.resize(2); if (!parseTypeExpr(te.elements[0])) return false; if (!expect(Token::Colon, "map type")) return false; if (!parseTypeExpr(te.elements[1])) return false; return expect(Token::RBrace, "map type"); }
        if (atName()) {
            const std::string& name = current().text;
            te.kind = builtinTypes().count(name) ? TypeExpr::Primitive : TypeExpr::Named;
            te.name = name; ++m_pos; return true;
        }
        error("Expected type expression, got '" + current().text + "'"); return false;
    }
};

} // namespace

ParseResult parse(const std::string& source)
{
    LexResult lexResult = tokenize(source);
    if (lexResult.hasError()) {
        ParseResult pr;
        pr.error = lexResult.error;
        pr.errorLine = lexResult.errorLine;
        pr.errorColumn = lexResult.errorColumn;
        return pr;
    }
    Parser parser(lexResult.tokens);
    return parser.parse();
}

} // namespace lidl
