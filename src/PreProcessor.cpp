/*
 * PreProcessor.cpp
 * 
 * This file is part of the "HLSL Translator" (Copyright (c) 2014 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "PreProcessor.h"
#include <sstream>


namespace HTLib
{


PreProcessor::PreProcessor(IncludeHandler& includeHandler, Log* log) :
    Parser          { log            },
    includeHandler_ { includeHandler },
    scanner_        { log            }
{
}

std::shared_ptr<std::iostream> PreProcessor::Process(const std::shared_ptr<SourceCode>& input)
{
    PushScannerSource(input);

    output_ = std::make_shared<std::stringstream>();

    try
    {
        ParseProgram();

        #ifdef _DEBUG
        auto output = output_->str();
        #endif

        return output_;
    }
    catch (const Report& err)
    {
        if (GetLog())
            GetLog()->SumitReport(err);
    }

    return nullptr;
}


/*
 * ======= Private: =======
 */

ScannerPtr PreProcessor::MakeScanner()
{
    return std::make_shared<PreProcessorScanner>(GetLog());
}

PreProcessor::DefinedSymbolPtr PreProcessor::MakeSymbol(const std::string& ident)
{
    /* Make new symbol */
    auto symbol = std::make_shared<DefinedSymbol>();
    definedSymbols_[ident] = symbol;
    return symbol;
}

bool PreProcessor::IsDefined(const std::string& ident) const
{
    return (definedSymbols_.find(ident) != definedSymbols_.end());
}

bool PreProcessor::CompareTokenStrings(const TokenString& lhs, const TokenString& rhs) const
{
    auto IsTokenOfInteres = [](const std::vector<TokenPtr>::const_iterator& it)
    {
        auto type = (*it)->Type();
        return (type != Tokens::Comment && type != Tokens::WhiteSpaces && type != Tokens::NewLines);
    };

    auto NextStringToken = [&](const TokenString& tokenString, std::vector<TokenPtr>::const_iterator& it)
    {
        do
        {
            ++it;
        }
        while (it != tokenString.end() && !IsTokenOfInteres(it));
    };

    /* Get first tokens */
    auto lhsIt = lhs.begin(), rhsIt = rhs.begin();

    if (lhsIt != lhs.end() && !IsTokenOfInteres(lhsIt))
        NextStringToken(lhs, lhsIt);
    if (rhsIt != rhs.end() && !IsTokenOfInteres(rhsIt))
        NextStringToken(rhs, rhsIt);

    /* Check if all tokens of interest are equal in both strings */
    for (; (lhsIt != lhs.end() && rhsIt != rhs.end()); NextStringToken(lhs, lhsIt), NextStringToken(rhs, rhsIt))
    {
        auto lhsTkn = lhsIt->get();
        auto rhsTkn = rhsIt->get();

        /* Compare types */
        if (lhsTkn->Type() != rhsTkn->Type())
            return false;

        /* Compare values */
        if (lhsTkn->Spell() != rhsTkn->Spell())
            return false;
    }

    /* Check if both strings reached the end */
    return (lhsIt == lhs.end() && rhsIt == rhs.end());
}

void PreProcessor::OutputTokenString(const TokenString& tokenString)
{
    for (const auto& tkn : tokenString)
        *output_ << tkn->Spell();
}

void PreProcessor::PushIfBlock(bool active)
{
    activeIfBlockStack_.push(IsTopIfBlockActive() && active);
}

void PreProcessor::PopIfBlock()
{
    if (activeIfBlockStack_.empty())
        Error("missing '#if'-directive to closing '#endif'");
    activeIfBlockStack_.pop();
}

bool PreProcessor::IsTopIfBlockActive() const
{
    return (activeIfBlockStack_.empty() || activeIfBlockStack_.top());
}

void PreProcessor::SkipToNextLine()
{
    while (!Is(Tokens::NewLines))
        AcceptIt();
    AcceptIt();
}

/* === Parse functions === */

void PreProcessor::ParseProgram()
{
    do
    {
        while (!Is(Tokens::EndOfStream))
        {
            if (IsTopIfBlockActive())
            {
                /* Parse active block */
                switch (Type())
                {
                    case Tokens::Directive:
                        ParseDirective();
                        break;
                    case Tokens::Comment:
                        ParesComment();
                        break;
                    case Tokens::Ident:
                        ParseIdent();
                        break;
                    default:
                        ParseMisc();
                        break;
                }
            }
            else
            {
                /* On an inactive if-block: parse only '#if'-directives or skip to next line */
                if (Type() == Tokens::Directive)
                    ParseAnyIfDirectiveAndSkipValidation();
                SkipToNextLine();
            }
        }
    }
    while (PopScannerSource());
}

void PreProcessor::ParesComment()
{
    *output_ << Accept(Tokens::Comment)->Spell();
}

void PreProcessor::ParseIdent()
{
    auto ident = Accept(Tokens::Ident)->Spell();

    /* Search for defined symbol */
    auto it = definedSymbols_.find(ident);
    if (it != definedSymbols_.end())
    {
        auto& symbol = *it->second;

        if (!symbol.parameters.empty())
        {
            //TODO...
        }
        else
            OutputTokenString(symbol.tokenString);
    }
    else
        *output_ << ident;
}

void PreProcessor::ParseMisc()
{
    *output_ << AcceptIt()->Spell();
}

void PreProcessor::ParseDirective()
{
    /* Parse pre-processor directive */
    auto tkn = Accept(Tokens::Directive);
    auto directive = tkn->Spell();

    if (directive == "define")
        ParseDirectiveDefine();
    else if (directive == "undef")
        ParseDirectiveUndef();
    else if (directive == "include")
        ParseDirectiveInclude();
    else if (directive == "if")
        ParseDirectiveIf();
    else if (directive == "ifdef")
        ParseDirectiveIfdef();
    else if (directive == "ifndef")
        ParseDirectiveIfndef();
    else if (directive == "elif")
        ParseDirectiveElif();
    else if (directive == "else")
        ParseDirectiveElse();
    else if (directive == "endif")
        ParseDirectiveEndif();
    else if (directive == "pragma")
        ParseDirectivePragma();
    else if (directive == "line")
        ParseDirectiveLine();
    else if (directive == "error")
        ParseDirectiveError(tkn);
    else
        Error("unknown preprocessor directive: \"" + directive + "\"");
}

void PreProcessor::ParseAnyIfDirectiveAndSkipValidation()
{
    /* Parse pre-processor directive */
    auto tkn = Accept(Tokens::Directive);
    auto directive = tkn->Spell();

    if (directive == "if")
        ParseDirectiveIf(true);
    else if (directive == "ifdef")
        ParseDirectiveIfdef(true);
    else if (directive == "ifndef")
        ParseDirectiveIfndef(true);
    else if (directive == "elif")
        ParseDirectiveElif(true);
    else if (directive == "else")
        ParseDirectiveElse();
    else if (directive == "endif")
        ParseDirectiveEndif();
}

// '#define' IDENT ( '(' IDENT+ ')' )? (TOKEN-STRING)?
void PreProcessor::ParseDirectiveDefine()
{
    /* Parse identifier */
    IgnoreWhiteSpaces(false);
    auto ident = Accept(Tokens::Ident)->Spell();

    /* Check if identifier is already defined */
    DefinedSymbolPtr previousSymbol;
    auto previousSymbolIt = definedSymbols_.find(ident);
    if (previousSymbolIt != definedSymbols_.end())
        previousSymbol = previousSymbolIt->second;

    /* Make new defined symbol */
    auto symbol = MakeSymbol(ident);

    /* Ignore white spaces and check for end of line */
    IgnoreWhiteSpaces(false);
    if (Is(Tokens::NewLines))
        return;

    /* Parse optional parameters */
    if (Is(Tokens::LBracket))
    {
        AcceptIt();
        IgnoreWhiteSpaces(false);

        if (!Is(Tokens::RBracket))
        {
            while (true)
            {
                /* Parse next parameter identifier */
                IgnoreWhiteSpaces(false);
                auto paramIdent = Accept(Tokens::Ident)->Spell();
                IgnoreWhiteSpaces(false);

                symbol->parameters.push_back(paramIdent);

                /* Check if parameter list is finished */
                if (!Is(Tokens::Comma))
                    break;

                AcceptIt();
            }
        }
        
        Accept(Tokens::RBracket);
    }

    /* Parse optional value */
    symbol->tokenString = ParseTokenString();

    /* Now compare previous and new definition */
    if (previousSymbol)
    {
        /* Compare parameters */
        //TODO...

        /* Compare values */
        if (CompareTokenStrings(previousSymbol->tokenString, symbol->tokenString))
            Warning("redefinition of symbol \"" + ident + "\"");
        else
            Error("redefinition of symbol \"" + ident + "\" with mismatch");
    }
}

void PreProcessor::ParseDirectiveUndef()
{
    /* Parse identifier */
    IgnoreWhiteSpaces(false);
    auto ident = Accept(Tokens::Ident)->Spell();

    /* Remove symbol */
    auto it = definedSymbols_.find(ident);
    if (it != definedSymbols_.end())
        definedSymbols_.erase(it);
    else
        Warning("failed to undefine symbol \"" + ident + "\"");
}

void PreProcessor::ParseDirectiveInclude()
{
    /* Parse filename string literal */
    IgnoreWhiteSpaces(false);
    auto filename = Accept(Tokens::StringLiteral)->Spell();
    
    /* Check if filename has already been marked as 'once included' */
    if (onceIncluded_.find(filename) == onceIncluded_.end())
    {
        /* Open source code */
        std::unique_ptr<std::istream> includeStream;

        try
        {
            includeStream = includeHandler_.Include(filename);
        }
        catch (const std::exception& e)
        {
            Error(e.what());
        }

        /* Push scanner soruce for include file */
        auto sourceCode = std::make_shared<SourceCode>(std::move(includeStream));
        PushScannerSource(sourceCode, filename);
    }
}

// '#if' CONSTANT-EXPRESSION
void PreProcessor::ParseDirectiveIf(bool skipEvaluation)
{
    //todo...
}

// '#ifdef' IDENT
void PreProcessor::ParseDirectiveIfdef(bool skipEvaluation)
{
    if (skipEvaluation)
    {
        /* Push new if-block activation (and skip evaluation, due to currently inactive block) */
        PushIfBlock();
    }
    else
    {
        /* Parse identifier */
        IgnoreWhiteSpaces(false);
        auto ident = Accept(Tokens::Ident)->Spell();
    
        /* Push new if-block activation (with 'defined' condition) */
        PushIfBlock(IsDefined(ident));
    }
}

// '#ifndef' IDENT
void PreProcessor::ParseDirectiveIfndef(bool skipEvaluation)
{
    /* Parse identifier */
    IgnoreWhiteSpaces(false);
    auto ident = Accept(Tokens::Ident)->Spell();
    
    /* Push new if-block activation (with 'not defined' condition) */
    PushIfBlock(!IsDefined(ident));
}

// '#elif CONSTANT-EXPRESSION'
void PreProcessor::ParseDirectiveElif(bool skipEvaluation)
{
    //todo...
}

// '#else'
void PreProcessor::ParseDirectiveElse()
{
    //todo...
}

// '#endif'
void PreProcessor::ParseDirectiveEndif()
{
    /* Only pop if-block from top of the stack */
    PopIfBlock();
}

// '#pragma' TOKEN-STRING
// see https://msdn.microsoft.com/de-de/library/windows/desktop/dd607351(v=vs.85).aspx
void PreProcessor::ParseDirectivePragma()
{
    /* Parse pragma command identifier */
    IgnoreWhiteSpaces(false);
    auto command = Accept(Tokens::Ident)->Spell();

    if (command == "once")
    {
        /* Mark current filename as 'once included' (but not for the main file) */
        auto filename = GetCurrentFilename();
        if (!filename.empty())
            onceIncluded_.insert(std::move(filename));
    }
    else
        Warning("unknown pragma command: \"" + command + "\"");
}

// '#line' NUMBER STRING-LITERAL?
void PreProcessor::ParseDirectiveLine()
{
    /* Parse line number */
    IgnoreWhiteSpaces(false);
    auto lineNumber = Accept(Tokens::IntLiteral)->Spell();

    /* Parse optional filename */
    IgnoreWhiteSpaces(false);

    if (Is(Tokens::StringLiteral))
    {
        auto filename = AcceptIt()->Spell();

        //TODO...
    }
}

// '#error' TOKEN-STRING
void PreProcessor::ParseDirectiveError(const TokenPtr& directiveToken)
{
    auto pos = directiveToken->Pos();

    /* Parse token string */
    auto tokenString = ParseTokenString();
    
    /* Convert token string into error message */
    std::string errorMsg;

    for (const auto& tkn : tokenString)
    {
        if (tkn->Type() == Tokens::StringLiteral)
            errorMsg += '\"' + tkn->Spell() + '\"';
        else
            errorMsg += tkn->Spell();
    }

    throw Report(Report::Types::Error, "error (" + pos.ToString() + ") : " + errorMsg);
}

PreProcessor::TokenString PreProcessor::ParseTokenString()
{
    TokenString tokenString;

    IgnoreWhiteSpaces(false);

    while (!Is(Tokens::NewLines))
    {
        switch (Type())
        {
            case Tokens::LineBreak:
                AcceptIt();
                IgnoreWhiteSpaces(false);
                while (Is(Tokens::NewLines))
                    tokenString.push_back(AcceptIt());
                break;

            default:
                tokenString.push_back(AcceptIt());
                break;
        }
    }

    return tokenString;
}


} // /namespace HTLib



// ================================================================================