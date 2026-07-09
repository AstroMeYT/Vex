#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <memory>
#include <sstream>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <regex>

// POSIX Headers for Linux-based environment features
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>

// ==========================================
// Value Representation System
// ==========================================
enum class ValueType { NUMBER, STRING, BOOLEAN, NONE };

struct Value {
    ValueType type = ValueType::NONE;
    double num_val = 0.0;
    std::string str_val = "";
    bool bool_val = false;

    Value() : type(ValueType::NONE) {}
    Value(double n) : type(ValueType::NUMBER), num_val(n) {}
    Value(std::string s) : type(ValueType::STRING), str_val(s) {}
    Value(bool b) : type(ValueType::BOOLEAN), bool_val(b) {}

    std::string to_string() const {
        if (type == ValueType::NUMBER) {
            if (std::isnan(num_val)) return "nan";
            if (std::isinf(num_val)) return num_val > 0 ? "inf" : "-inf";

            // If it's a flat integer, print it without decimal points or scientific notation
            if (num_val >= -9007199254740992.0 && num_val <= 9007199254740992.0) {
                if (num_val == std::floor(num_val)) {
                    std::ostringstream oss;
                    oss << std::fixed;
                    oss.precision(0);
                    oss << num_val;
                    return oss.str();
                }
            }

            // For floats, use fixed layout with maximum double precision, stripping trailing zeroes
            std::ostringstream oss;
            oss << std::fixed;
            oss.precision(16); // Maximum useful precision for standard 64-bit double
            oss << num_val;
            std::string s = oss.str();
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (!s.empty() && s.back() == '.') s.pop_back();
            return s;
        }
        if (type == ValueType::BOOLEAN) return bool_val ? "True" : "False";
        if (type == ValueType::STRING) return str_val;
        return "0";
    }

    bool to_bool() const {
        if (type == ValueType::BOOLEAN) return bool_val;
        if (type == ValueType::NUMBER) return num_val != 0;
        if (type == ValueType::STRING) return !str_val.empty() && str_val != "False" && str_val != "false";
        return false;
    }

    double to_number() const {
        if (type == ValueType::NUMBER) return num_val;
        if (type == ValueType::BOOLEAN) return bool_val ? 1.0 : 0.0;
        if (type == ValueType::STRING) {
            try {
                size_t idx;
                double res = std::stod(str_val, &idx);
                return res;
            } catch (...) { return 0.0; }
        }
        return 0.0;
    }
};

// Global variables mappings
typedef std::unordered_map<std::string, Value> Scope;

// AST Nodes
struct ASTNode {
    std::string line;
    std::vector<ASTNode> children;
};

// ==========================================
// Static Analysis & Profile Configurations
// ==========================================
struct ExecutionState {
    std::string active_env = "main";
    std::vector<std::string> blocked_statements;
    std::unordered_map<std::string, std::string> libraries; // alias -> exec_path mapping
};

// Forward declaration of the evaluate function for recursive AST evaluations
Value evaluate(std::string expr, Scope& local_vars, Scope& global_vars, ExecutionState& state);

// ==========================================
// Expression Tokenizer & Parser (The eval engine)
// ==========================================
enum class TokenType {
    NUMBER, STRING, IDENTIFIER,
    PLUS, MINUS, MULTIPLY, DIVIDE, MODULO,
    EQUAL, LESS, GREATER, LESSEQUAL, GREATEREQUAL,
    LPAREN, RPAREN, COMMA, DOT,
    AND, OR, NOT, TRUE_LIT, FALSE_LIT, END
};

struct Token {
    TokenType type;
    std::string text;
};

class Lexer {
    std::string src;
    size_t pos = 0;

    char peek() { return pos < src.size() ? src[pos] : '\0'; }
    char advance() { return pos < src.size() ? src[pos++] : '\0'; }

public:
    Lexer(std::string text) : src(text) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (pos < src.size()) {
            char c = peek();
            if (std::isspace(c)) {
                advance();
                continue;
            }
            if (c == '.') {
                if (std::isdigit(src[pos + 1])) {
                    std::string num = "";
                    while (std::isdigit(peek()) || peek() == '.') {
                        num += advance();
                    }
                    tokens.push_back({TokenType::NUMBER, num});
                } else {
                    advance();
                    tokens.push_back({TokenType::DOT, "."});
                }
                continue;
            }
            if (std::isdigit(c)) {
                std::string num = "";
                while (std::isdigit(peek()) || peek() == '.') {
                    num += advance();
                }
                tokens.push_back({TokenType::NUMBER, num});
                continue;
            }
            if (c == '"' || c == '\'') {
                char quote = advance();
                std::string str = "";
                while (peek() != '\0' && peek() != quote) {
                    str += advance();
                }
                if (peek() == quote) advance();
                tokens.push_back({TokenType::STRING, str});
                continue;
            }
            if (std::isalpha(c) || c == '_') {
                std::string id = "";
                while (std::isalnum(peek()) || peek() == '_') {
                    id += advance();
                }
                if (id == "and") tokens.push_back({TokenType::AND, id});
                else if (id == "or") tokens.push_back({TokenType::OR, id});
                else if (id == "not") tokens.push_back({TokenType::NOT, id});
                else if (id == "True" || id == "true") tokens.push_back({TokenType::TRUE_LIT, "True"});
                else if (id == "False" || id == "false") tokens.push_back({TokenType::FALSE_LIT, "False"});
                else tokens.push_back({TokenType::IDENTIFIER, id});
                continue;
            }
            if (c == '=') {
                advance();
                if (peek() == '=') { advance(); tokens.push_back({TokenType::EQUAL, "=="}); }
                else { tokens.push_back({TokenType::EQUAL, "="}); } // Vex '=' acts as comparison '=='
                continue;
            }
            if (c == '<') {
                advance();
                if (peek() == '=') { advance(); tokens.push_back({TokenType::LESSEQUAL, "<="}); }
                else { tokens.push_back({TokenType::LESS, "<"}); }
                continue;
            }
            if (c == '>') {
                advance();
                if (peek() == '=') { advance(); tokens.push_back({TokenType::GREATEREQUAL, ">="}); }
                else { tokens.push_back({TokenType::GREATER, ">"}); }
                continue;
            }
            if (c == '+') { advance(); tokens.push_back({TokenType::PLUS, "+"}); continue; }
            if (c == '-') { advance(); tokens.push_back({TokenType::MINUS, "-"}); continue; }
            if (c == '*') { advance(); tokens.push_back({TokenType::MULTIPLY, "*"}); continue; }
            if (c == '/') { advance(); tokens.push_back({TokenType::DIVIDE, "/"}); continue; }
            if (c == '%') { advance(); tokens.push_back({TokenType::MODULO, "%"}); continue; }
            if (c == '(') { advance(); tokens.push_back({TokenType::LPAREN, "("}); continue; }
            if (c == ')') { advance(); tokens.push_back({TokenType::RPAREN, ")"}); continue; }
            if (c == ',') { advance(); tokens.push_back({TokenType::COMMA, ","}); continue; }

            advance(); // Consume unrecognized tokens
        }
        tokens.push_back({TokenType::END, ""});
        return tokens;
    }
};

class Parser {
    std::vector<Token> tokens;
    size_t index = 0;
    Scope& local_scope;
    Scope& global_scope;
    ExecutionState& state;

    Token peek() { return tokens[index]; }
    Token advance() { return tokens[index++]; }

    Value lookup_var(const std::string& name) {
        if (local_scope.count(name)) return local_scope[name];
        if (global_scope.count(name)) return global_scope[name];
        return Value(0.0); // Default uninitialized to 0
    }

    Value primary() {
        Token t = peek();
        
        if (t.type == TokenType::IDENTIFIER) {
            // 1. Built-in input / input.number
            if (t.text == "input") {
                advance(); // consume "input"
                if (peek().type == TokenType::DOT) {
                    advance(); // consume "."
                    Token member = peek();
                    if (member.type == TokenType::IDENTIFIER && member.text == "number") {
                        advance(); // consume "number"
                        if (peek().type == TokenType::LPAREN) {
                            advance(); // consume "("
                            Value prompt("");
                            if (peek().type != TokenType::RPAREN) {
                                prompt = expression();
                            }
                            if (peek().type == TokenType::RPAREN) advance(); // consume ")"
                            
                            std::string prompt_str = prompt.to_string();
                            std::cout << prompt_str << std::flush;
                            std::string user_in;
                            if (!std::getline(std::cin, user_in)) {
                                return Value(0.0);
                            }
                            try {
                                size_t idx = 0;
                                size_t first = user_in.find_first_not_of(" \t\r\n");
                                size_t last = user_in.find_last_not_of(" \t\r\n");
                                if (first != std::string::npos) {
                                    std::string trimmed = user_in.substr(first, (last - first + 1));
                                    double num = std::stod(trimmed, &idx);
                                    if (idx == trimmed.size()) return Value(num);
                                }
                            } catch (...) {}
                            throw std::runtime_error("Invalid numeric input to input.number()");
                        }
                    }
                } else if (peek().type == TokenType::LPAREN) {
                    advance(); // consume "("
                    Value prompt("");
                    if (peek().type != TokenType::RPAREN) {
                        prompt = expression();
                    }
                    if (peek().type == TokenType::RPAREN) advance(); // consume ")"
                    
                    std::cout << prompt.to_string() << std::flush;
                    std::string user_in;
                    if (std::getline(std::cin, user_in)) {
                        return Value(user_in);
                    }
                    return Value("");
                }
                return lookup_var("input");
            }

            // 2. Custom Addon Libraries Execution
            if (state.libraries.count(t.text)) {
                advance(); // consume library alias
                std::string exec_path = state.libraries[t.text];
                std::vector<std::string> opcodes;
                
                // Parse opcodes via dot-notation
                while (peek().type == TokenType::DOT) {
                    advance(); // consume '.'
                    Token member = peek();
                    if (member.type == TokenType::IDENTIFIER) {
                        opcodes.push_back(member.text);
                        advance();
                    } else {
                        throw std::runtime_error("Expected identifier after '.' in library call");
                    }
                }
                
                if (peek().type == TokenType::LPAREN) {
                    advance(); // consume '('
                    
                    std::vector<std::string> args;
                    while (peek().type != TokenType::RPAREN && peek().type != TokenType::END) {
                        std::string arg_key = "";
                        
                        // Lookahead to see if argument is a Key=Value mapping
                        if (peek().type == TokenType::IDENTIFIER) {
                            // Check the NEXT token (index + 1) for the equals sign, not the current one
                            if (index + 1 < tokens.size() && tokens[index + 1].type == TokenType::EQUAL) {
                                arg_key = peek().text;
                                advance(); // consume Key identifier
                                advance(); // consume '='
                            }
                        }
                        
                        Value arg_val = expression();
                        if (!arg_key.empty()) {
                            args.push_back(arg_key + "=\"" + arg_val.to_string() + "\"");
                        } else {
                            args.push_back("\"" + arg_val.to_string() + "\"");
                        }
                        
                        if (peek().type == TokenType::COMMA) advance();
                    }
                    if (peek().type == TokenType::RPAREN) advance(); // consume ')'
                    
                    // Compile command
                    std::string cmd = exec_path;
                    for (const auto& op : opcodes) cmd += " " + op;
                    for (const auto& a : args) cmd += " " + a;
                    
                    // Execute Shell Popen command
                    char buffer[128];
                    std::string result = "";
                    FILE* pipe = popen(cmd.c_str(), "r");
                    if (!pipe) throw std::runtime_error("popen() failed executing library!");
                    
                    try {
                        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                            result += buffer;
                        }
                    } catch (...) {
                        pclose(pipe);
                        throw;
                    }
                    pclose(pipe);
                    
                    // Format Output
                    if (!result.empty()) {
                        result.erase(result.find_last_not_of(" \n\r\t") + 1);
                    }
                    
                    try {
                        size_t idx;
                        double d = std::stod(result, &idx);
                        if (idx == result.size()) return Value(d);
                    } catch (...) {}
                    
                    return Value(result);
                } else {
                    return Value(0.0);
                }
            }

            // 3. Normal variable fallback
            advance(); // consume identifier
            return lookup_var(t.text);
        }
        
        if (t.type == TokenType::NUMBER) {
            advance();
            return Value(std::stod(t.text));
        }
        if (t.type == TokenType::STRING) {
            advance();
            return Value(t.text);
        }
        if (t.type == TokenType::TRUE_LIT) {
            advance();
            return Value(true);
        }
        if (t.type == TokenType::FALSE_LIT) {
            advance();
            return Value(false);
        }
        if (t.type == TokenType::LPAREN) {
            advance();
            Value val = expression();
            if (peek().type == TokenType::RPAREN) advance();
            return val;
        }
        if (t.type == TokenType::NOT) {
            advance();
            return Value(!primary().to_bool());
        }
        return Value(0.0);
    }

    Value factor() {
        Value val = primary();
        while (peek().type == TokenType::MULTIPLY || peek().type == TokenType::DIVIDE || peek().type == TokenType::MODULO) {
            Token op = advance();
            Value right = primary();
            if (op.type == TokenType::MULTIPLY) {
                val = Value(val.to_number() * right.to_number());
            } else if (op.type == TokenType::DIVIDE) {
                double denominator = right.to_number();
                if (denominator == 0.0) {
                    throw std::runtime_error("Zero division error");
                }
                val = Value(val.to_number() / denominator);
            } else if (op.type == TokenType::MODULO) {
                double denominator = right.to_number();
                val = Value(denominator != 0.0 ? std::fmod(val.to_number(), denominator) : 0.0);
            }
        }
        return val;
    }

    Value term() {
        Value val = factor();
        while (peek().type == TokenType::PLUS || peek().type == TokenType::MINUS) {
            Token op = advance();
            Value right = factor();
            if (op.type == TokenType::PLUS) {
                if (val.type == ValueType::STRING || right.type == ValueType::STRING) {
                    val = Value(val.to_string() + right.to_string());
                } else {
                    val = Value(val.to_number() + right.to_number());
                }
            } else if (op.type == TokenType::MINUS) {
                val = Value(val.to_number() - right.to_number());
            }
        }
        return val;
    }

    Value comparison() {
        Value val = term();
        while (peek().type == TokenType::EQUAL || peek().type == TokenType::LESS ||
               peek().type == TokenType::GREATER || peek().type == TokenType::LESSEQUAL ||
               peek().type == TokenType::GREATEREQUAL) {
            Token op = advance();
            Value right = term();
            if (op.type == TokenType::EQUAL) {
                if (val.type == ValueType::STRING || right.type == ValueType::STRING) {
                    val = Value(val.to_string() == right.to_string());
                } else {
                    val = Value(val.to_number() == right.to_number());
                }
            } else if (op.type == TokenType::LESS) {
                val = Value(val.to_number() < right.to_number());
            } else if (op.type == TokenType::GREATER) {
                val = Value(val.to_number() > right.to_number());
            } else if (op.type == TokenType::LESSEQUAL) {
                val = Value(val.to_number() <= right.to_number());
            } else if (op.type == TokenType::GREATEREQUAL) {
                val = Value(val.to_number() >= right.to_number());
            }
        }
        return val;
    }

    Value logical_and() {
        Value val = comparison();
        while (peek().type == TokenType::AND) {
            advance();
            Value right = comparison();
            val = Value(val.to_bool() && right.to_bool());
        }
        return val;
    }

public:
    Parser(std::vector<Token> t, Scope& l, Scope& g, ExecutionState& s) : tokens(t), local_scope(l), global_scope(g), state(s) {}

    Value expression() {
        Value val = logical_and();
        while (peek().type == TokenType::OR) {
            advance();
            Value right = logical_and();
            val = Value(val.to_bool() || right.to_bool());
        }
        return val;
    }
};

// ==========================================
// Keystroke / Terminal Event Listener Helper
// ==========================================
bool check_key_pressed(const std::string& key_name) {
    struct termios orig_t;
    if (tcgetattr(STDIN_FILENO, &orig_t) == -1) return false;
    
    struct termios raw = orig_t;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    
    bool pressed = false;
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
        char ch = 0;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            if (key_name == "space" && ch == ' ') {
                pressed = true;
            } else if (key_name == "enter" && ch == '\n') {
                pressed = true;
            } else if (key_name.size() == 1 && ch == key_name[0]) {
                pressed = true;
            } else if (ch == 27) { // ANSI escape sequences (arrows)
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'A' && key_name == "up") pressed = true;
                        else if (seq[1] == 'B' && key_name == "down") pressed = true;
                        else if (seq[1] == 'C' && key_name == "right") pressed = true;
                        else if (seq[1] == 'D' && key_name == "left") pressed = true;
                    }
                }
            }
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_t);
    return pressed;
}

// Helper evaluator wrapper
Value evaluate(std::string expr, Scope& local_vars, Scope& global_vars, ExecutionState& state) {
    // Dynamic matching of button events
    std::regex event_pattern("event:button-down\\.([a-zA-Z0-9_-]+)");
    std::smatch m;
    while (std::regex_search(expr, m, event_pattern)) {
        std::string key_name = m[1].str();
        bool pressed = check_key_pressed(key_name);
        expr.replace(m.position(0), m.length(0), pressed ? "True" : "False");
    }

    // Normalizations for fallback general expressions
    size_t event_pos;
    while ((event_pos = expr.find("event:")) != std::string::npos) {
        size_t end_pos = event_pos;
        while (end_pos < expr.size() && (std::isalnum(expr[end_pos]) || expr[end_pos] == '-' || expr[end_pos] == '.' || expr[end_pos] == ':')) {
            end_pos++;
        }
        expr.replace(event_pos, end_pos - event_pos, "False");
    }

    Lexer lexer(expr);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens, local_vars, global_vars, state);
    return parser.expression();
}

// ==========================================
// Parsing & Preprocessing Structure
// ==========================================
std::string strip_comments_and_normalize(std::string line) {
    size_t pos;
    while ((pos = line.find("“")) != std::string::npos) line.replace(pos, 3, "\"");
    while ((pos = line.find("”")) != std::string::npos) line.replace(pos, 3, "\"");
    while ((pos = line.find("‘")) != std::string::npos) line.replace(pos, 3, "'");
    while ((pos = line.find("’")) != std::string::npos) line.replace(pos, 3, "'");

    bool in_string = false;
    char string_char = '\0';
    for (size_t i = 0; i < line.size(); i++) {
        if ((line[i] == '"' || line[i] == '\'') && (i == 0 || line[i-1] != '\\')) {
            if (!in_string) {
                in_string = true;
                string_char = line[i];
            } else if (line[i] == string_char) {
                in_string = false;
            }
        } else if (line[i] == '#' && !in_string) {
            line = line.substr(0, i);
            break;
        }
    }
    while (!line.empty() && std::isspace(line.back())) {
        line.pop_back();
    }
    return line;
}

std::vector<std::string> preprocess_code(const std::string& code) {
    std::vector<std::string> lines;
    std::string current_line;
    std::stringstream ss(code);
    std::string buffer = "";
    int in_parens = 0;

    while (std::getline(ss, current_line)) {
        std::string line = strip_comments_and_normalize(current_line);
        if (line.find_first_not_of(" \t\r\n") == std::string::npos && buffer.empty()) {
            continue;
        }

        bool in_str = false;
        char str_c = '\0';
        for (char c : line) {
            if (c == '"' || c == '\'') {
                if (!in_str) { in_str = true; str_c = c; }
                else if (c == str_c) { in_str = false; }
            }
            if (!in_str) {
                if (c == '(') in_parens++;
                else if (c == ')') in_parens--;
            }
        }

        if (!buffer.empty()) {
            size_t start = line.find_first_not_of(" \t");
            std::string trimmed = (start == std::string::npos) ? "" : line.substr(start);
            buffer += " " + trimmed;
        } else {
            buffer = line;
        }

        if (in_parens <= 0) {
            lines.push_back(buffer);
            buffer = "";
            in_parens = 0;
        }
    }
    return lines;
}

std::vector<ASTNode> parse_blocks(const std::vector<std::string>& lines) {
    std::vector<ASTNode> root;
    std::vector<std::pair<int, std::vector<ASTNode>*>> stack;
    stack.push_back({-1, &root});

    for (const auto& line : lines) {
        if (line.find_first_not_of(" \t") == std::string::npos) continue;

        size_t indent = 0;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
            indent += (line[indent] == '\t' ? 4 : 1);
        }

        std::string stripped = line;
        size_t non_space = stripped.find_first_not_of(" \t");
        if (non_space != std::string::npos) {
            stripped = stripped.substr(non_space);
        }

        while (stack.size() > 1 && stack.back().first >= static_cast<int>(indent)) {
            stack.pop_back();
        }

        ASTNode new_node{stripped, {}};
        stack.back().second->push_back(new_node);
        stack.push_back({static_cast<int>(indent), &(stack.back().second->back().children)});
    }
    return root;
}

// ==========================================
// Advanced Library & Environment Configs
// ==========================================

const std::unordered_set<std::string> RESERVED_KEYWORDS = {
    "globalvar", "localvar", "globvar", "function", "dofunc", "if", "else", "until", 
    "while", "repeat", "addlib", "try", "catch", "exitloop", "wait", 
    "warn", "critical", "exit", "env"
};

bool is_blocked(const std::string& line, const std::vector<std::string>& blocked_list) {
    for (const auto& blocked : blocked_list) {
        std::regex pattern("^\\s*\\b" + blocked + "\\b");
        if (std::regex_search(line, pattern)) {
            return true;
        }
    }
    return false;
}

void pre_run_checks(const std::vector<ASTNode>& nodes, const ExecutionState& state) {
    for (const auto& node : nodes) {
        std::string line = node.line;

        if (is_blocked(line, state.blocked_statements)) {
            std::string first_cmd = line.substr(0, line.find_first_of(" \t("));
            std::cerr << "Environment Security Error: The command starting with '" << first_cmd 
                      << "' is blocked in the '" << state.active_env << "' environment.\n";
            std::exit(1);
        }

        std::regex var_pattern("^(?:globalvar|localvar|globvar)\\s+([a-zA-Z_]\\w*)");
        std::smatch match;
        if (std::regex_search(line, match, var_pattern)) {
            std::string name = match[1].str();
            bool is_digit = true;
            for (char c : name) if (!std::isdigit(c)) is_digit = false;
            if (is_digit) {
                std::cerr << "Syntax Error: Variable name '" << name << "' cannot consist only of numbers in '" << line << "'\n";
                std::exit(1);
            }
            if (RESERVED_KEYWORDS.count(name)) {
                std::cerr << "Syntax Error: Cannot use reserved keyword '" << name << "' as a variable name in '" << line << "'\n";
                std::exit(1);
            }
        }

        std::regex func_pattern("^function\\s+([a-zA-Z_]\\w*)");
        if (std::regex_search(line, match, func_pattern)) {
            std::string name = match[1].str();
            bool is_digit = true;
            for (char c : name) if (!std::isdigit(c)) is_digit = false;
            if (is_digit) {
                std::cerr << "Syntax Error: Function name '" << name << "' cannot consist only of numbers in '" << line << "'\n";
                std::exit(1);
            }
            if (RESERVED_KEYWORDS.count(name)) {
                std::cerr << "Syntax Error: Cannot use reserved keyword '" << name << "' as a function name in '" << line << "'\n";
                std::exit(1);
            }
        }

        if (!node.children.empty()) {
            pre_run_checks(node.children, state);
        }
    }
}

// Robust host OS platform detection
std::string get_os_base() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    #if defined(__aarch64__)
        return "macos-m-chip";
    #else
        return "macos-intel";
    #endif
#elif defined(__linux__)
    std::ifstream os_release("/etc/os-release");
    if (os_release.is_open()) {
        std::string line;
        std::string combined = "";
        while (std::getline(os_release, line)) {
            if (line.rfind("ID=", 0) == 0 || line.rfind("ID_LIKE=", 0) == 0) {
                for (char& c : line) c = std::tolower(c);
                combined += line + " ";
            }
        }
        if (combined.find("debian") != std::string::npos || combined.find("ubuntu") != std::string::npos) return "debian";
        if (combined.find("arch") != std::string::npos || combined.find("manjaro") != std::string::npos) return "arch";
        if (combined.find("rhel") != std::string::npos || combined.find("fedora") != std::string::npos || combined.find("centos") != std::string::npos) return "redhat";
    }
    return "debian"; // Default linux fallback structure
#else
    return "debian";
#endif
}

std::string get_env_directory() {
    const char* home = std::getenv("HOME");
    if (!home) return ".vex_environments";
    return std::string(home) + "/.vex_environments";
}

std::string get_lib_directory() {
    const char* home = std::getenv("HOME");
    if (!home) return ".vex_libraries";
    return std::string(home) + "/.vex_libraries";
}

void ensure_default_main_env() {
    std::string dir = get_env_directory();
    mkdir(dir.c_str(), 0777);
    std::string main_path = dir + "/main.json";
    
    struct stat st;
    if (stat(main_path.c_str(), &st) != 0) {
        std::ofstream f(main_path);
        if (f.is_open()) {
            f << "{\n"
              << "    \"name\": \"main\",\n"
              << "    \"max_mem_usage\": null,\n"
              << "    \"max_cpu_usage\": null,\n"
              << "    \"blocked_statements\": \"\"\n"
              << "}\n";
        }
    }
}

void apply_environment(const std::string& env_name, ExecutionState& state, bool explicit_flag = false) {
    std::string filepath = get_env_directory() + "/" + env_name + ".json";
    state.active_env = env_name;

    struct stat st;
    if (stat(filepath.c_str(), &st) == 0) {
        std::ifstream f(filepath);
        if (f.is_open()) {
            std::string raw_json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            
            std::regex blocked_regex("\"blocked_statements\"\\s*:\\s*\"([^\"]*)\"");
            std::smatch m;
            if (std::regex_search(raw_json, m, blocked_regex)) {
                std::string blocked_str = m[1].str();
                std::stringstream ss(blocked_str);
                std::string item;
                state.blocked_statements.clear();
                while (std::getline(ss, item, ',')) {
                    size_t first = item.find_first_not_of(" \t");
                    size_t last = item.find_last_not_of(" \t");
                    if (first != std::string::npos && last != std::string::npos) {
                        state.blocked_statements.push_back(item.substr(first, (last - first + 1)));
                    }
                }
            }

            std::regex mem_regex("\"max_mem_usage\"\\s*:\\s*(\\d+)");
            if (std::regex_search(raw_json, m, mem_regex)) {
                long long limit_mb = std::stoll(m[1].str());
                rlim_t limit_bytes = limit_mb * 1024 * 1024;
                rlimit rl;
                rl.rlim_cur = limit_bytes;
                rl.rlim_max = limit_bytes;
                setrlimit(RLIMIT_AS, &rl);
            }

            std::regex cpu_regex("\"max_cpu_usage\"\\s*:\\s*(\\d+)");
            if (std::regex_search(raw_json, m, cpu_regex)) {
                int cpu_percent = std::stoi(m[1].str());
                if (cpu_percent < 100) {
                    int nice_val = (100 - cpu_percent) * 19 / 100;
                    nice(nice_val);
                }
            }
        }
    } else {
        if (explicit_flag) {
            std::cerr << "Environment Error: The specified environment profile '" << env_name << "' does not exist on this system.\n";
            std::exit(1);
        }
        state.blocked_statements.clear();
    }
}

// Download utility for dynamically importing external C++ / Python Libraries from AstroMeYT GitHub
void ensure_library(const std::string& libname) {
    std::string lib_dir = get_lib_directory() + "/" + libname;
    std::string exec_path = lib_dir + "/" + libname;
    std::string json_path = lib_dir + "/library-info.json";
    
    struct stat st;
    if (stat(exec_path.c_str(), &st) != 0) {
        std::cout << "Downloading library '" << libname << "'...\n" << std::flush;
        std::string mkdir_cmd = "mkdir -p " + lib_dir;
        if (system(mkdir_cmd.c_str()) != 0) {}

        std::string base_url = "https://raw.githubusercontent.com/AstroMeYT/Vex/main/VexLibC/" + libname + "/";
        std::string json_url = base_url + "library-info.json";
        std::string dl_json = "curl -s -o " + json_path + " " + json_url;
        if (system(dl_json.c_str()) != 0) {}
        
        std::ifstream jf(json_path);
        if (!jf.is_open()) {
            std::cerr << "Error: Could not find library '" << libname << "' in VexLibC.\n";
            std::exit(1);
        }
        
        std::string raw_json((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
        
        if (raw_json.find("404: Not Found") != std::string::npos) {
            std::cerr << "Error: Library '" << libname << "' does not exist in VexLibC.\n";
            std::string rm_cmd = "rm -rf " + lib_dir;
            if (system(rm_cmd.c_str()) != 0) {}
            std::exit(1);
        }

        std::regex sys_regex("\"supported-systems\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        if (std::regex_search(raw_json, m, sys_regex)) {
            std::string supported = m[1].str();
            std::string my_os = get_os_base();
            if (supported.find(my_os) == std::string::npos) {
                std::cerr << "Error: Library '" << libname << "' does not support your system (" << my_os << ").\n";
                std::cerr << "Supported systems: " << supported << "\n";
                std::string rm_cmd = "rm -rf " + lib_dir;
                if (system(rm_cmd.c_str()) != 0) {}
                std::exit(1);
            }
        } else {
            std::cerr << "Warning: library-info.json missing 'supported-systems'. Attempting download anyway.\n";
        }
        
        std::string exec_url = base_url + libname;
        std::string dl_exec = "curl -s -o " + exec_path + " " + exec_url;
        if (system(dl_exec.c_str()) != 0) {}
        
        // Final existence and permission check
        if (stat(exec_path.c_str(), &st) != 0) {
            std::cerr << "Error: Failed to download executable for library '" << libname << "'.\n";
            std::exit(1);
        }
        
        chmod(exec_path.c_str(), 0755); // Explicit CHMOD required to assign execution status locally 
        std::cout << "Library '" << libname << "' installed successfully.\n" << std::flush;
    }
}

// ==========================================
// Main Execution Engine Loop
// ==========================================
std::string run_ast(const std::vector<ASTNode>& nodes, Scope& local_vars, Scope& global_vars, 
                    std::unordered_map<std::string, std::vector<ASTNode>>& functions, ExecutionState& state) {
    bool last_if_result = false;
    bool last_try_failed = false;

    for (const auto& node : nodes) {
        std::string line = node.line;
        const auto& children = node.children;

        if (is_blocked(line, state.blocked_statements)) {
            std::cerr << "Environment Security Error: '" << line.substr(0, line.find_first_of(" \t(")) << "' is restricted.\n";
            std::exit(1);
        }

        if (line.rfind("addlib ", 0) == 0) {
            std::string rem = line.substr(7);
            rem.erase(0, rem.find_first_not_of(" \t"));
            std::string libname, alias;
            
            size_t as_pos = rem.find(" as ");
            if (as_pos != std::string::npos) {
                libname = rem.substr(0, as_pos);
                alias = rem.substr(as_pos + 4);
                libname.erase(libname.find_last_not_of(" \t") + 1);
                alias.erase(0, alias.find_first_not_of(" \t"));
            } else {
                libname = rem;
                alias = libname; // defaults to internal library name if no alias matches
            }
            
            ensure_library(libname);
            state.libraries[alias] = get_lib_directory() + "/" + libname + "/" + libname;
            continue;
        }
        else if (line.rfind("env ", 0) == 0) {
            std::string env_name = line.substr(4);
            env_name.erase(std::remove(env_name.begin(), env_name.end(), '\"'), env_name.end());
            env_name.erase(std::remove(env_name.begin(), env_name.end(), '\''), env_name.end());
            apply_environment(env_name, state, true);
        }
        else if (line.rfind("print(", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string args_str = line.substr(start, end - start);
            
            std::vector<std::string> parts;
            std::string current = "";
            bool in_str = false;
            char str_char = '\0';
            int paren_depth = 0;
            for (char c : args_str) {
                if (c == '"' || c == '\'') {
                    if (!in_str) { in_str = true; str_char = c; }
                    else if (c == str_char) { in_str = false; }
                } else if (!in_str) {
                    if (c == '(') paren_depth++;
                    else if (c == ')') paren_depth--;
                    else if (c == ',' && paren_depth == 0) {
                        parts.push_back(current);
                        current = "";
                        continue;
                    }
                }
                current += c;
            }
            if (!current.empty()) parts.push_back(current);

            std::string out = "";
            for (auto& part : parts) {
                Value val = evaluate(part, local_vars, global_vars, state);
                out += val.to_string();
            }

            size_t idx = 0;
            while ((idx = out.find("\\n", idx)) != std::string::npos) { out.replace(idx, 2, "\n"); idx += 1; }
            idx = 0;
            while ((idx = out.find("\\t", idx)) != std::string::npos) { out.replace(idx, 2, "\t"); idx += 1; }
            std::cout << out << "\n";
        }
        else if (line.rfind("globalvar ", 0) == 0 || line.rfind("localvar ", 0) == 0 || line.rfind("globvar ", 0) == 0) {
            std::regex var_reg("^(globalvar|localvar|globvar)\\s+([a-zA-Z_]\\w*)\\s*([=+\\-*/%^\\$])\\s*(.*)");
            std::smatch match;
            if (std::regex_search(line, match, var_reg)) {
                std::string vtype = match[1].str();
                std::string name = match[2].str();
                std::string op = match[3].str();
                std::string expr = match[4].str();

                Scope& target_env = (vtype == "globalvar" || vtype == "globvar") ? global_vars : local_vars;

                if (op == "$") {
                    target_env[name] = Value(!target_env[name].to_bool());
                } else {
                    Value val = evaluate(expr, local_vars, global_vars, state);
                    double curr = target_env.count(name) ? target_env[name].to_number() : 0.0;
                    if (op == "=") target_env[name] = val;
                    else if (op == "+") target_env[name] = Value(curr + val.to_number());
                    else if (op == "-") target_env[name] = Value(curr - val.to_number());
                    else if (op == "*") target_env[name] = Value(curr * val.to_number());
                    else if (op == "/") {
                        double denominator = val.to_number();
                        if (denominator == 0.0) throw std::runtime_error("Zero division error");
                        target_env[name] = Value(curr / denominator);
                    }
                    else if (op == "%") {
                        double denominator = val.to_number();
                        if (denominator == 0.0) throw std::runtime_error("Zero division error");
                        target_env[name] = Value(std::fmod(curr, denominator));
                    }
                }
            } else {
                throw std::runtime_error("Invalid variable assignment syntax: '" + line + "'");
            }
        }
        else if (line.rfind("if ", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string cond_str = line.substr(start, end - start);
            last_if_result = evaluate(cond_str, local_vars, global_vars, state).to_bool();
            if (last_if_result) {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (!res.empty()) return res;
            }
        }
        else if (line == "else") {
            if (!last_if_result) {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (!res.empty()) return res;
            }
        }
        else if (line.rfind("until ", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string cond_str = line.substr(start, end - start);
            while (!evaluate(cond_str, local_vars, global_vars, state).to_bool()) {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (res == "exitloop") break;
                if (!res.empty()) return res;
            }
        }
        else if (line.rfind("while ", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string cond_str = line.substr(start, end - start);
            while (evaluate(cond_str, local_vars, global_vars, state).to_bool()) {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (res == "exitloop") break;
                if (!res.empty()) return res;
            }
        }
        else if (line.rfind("repeat ", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string cond_str = line.substr(start, end - start);
            int times = static_cast<int>(evaluate(cond_str, local_vars, global_vars, state).to_number());
            for (int i = 0; i < times; i++) {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (res == "exitloop") break;
                if (!res.empty()) return res;
            }
        }
        else if (line.rfind("function ", 0) == 0) {
            std::string name = line.substr(9);
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            functions[name] = children;
        }
        else if (line.rfind("dofunc ", 0) == 0) {
            size_t start_paren = line.find('(');
            size_t end_paren = line.rfind(')');
            std::string func_name = line.substr(7, start_paren - 7);
            func_name.erase(0, func_name.find_first_not_of(" \t"));
            func_name.erase(func_name.find_last_not_of(" \t") + 1);

            std::string args_str = line.substr(start_paren + 1, end_paren - start_paren - 1);
            Scope kwargs;

            if (args_str.find_first_not_of(" \t") != std::string::npos) {
                std::vector<std::string> parts;
                std::string current = "";
                bool in_str = false;
                char str_char = '\0';
                int paren_depth = 0;
                for (char c : args_str) {
                    if (c == '"' || c == '\'') {
                        if (!in_str) { in_str = true; str_char = c; }
                        else if (c == str_char) { in_str = false; }
                    } else if (!in_str) {
                        if (c == '(') paren_depth++;
                        else if (c == ')') paren_depth--;
                        else if (c == ',' && paren_depth == 0) {
                            parts.push_back(current);
                            current = "";
                            continue;
                        }
                    }
                    current += c;
                }
                if (!current.empty()) parts.push_back(current);

                for (auto& part : parts) {
                    size_t eq = part.find('=');
                    if (eq != std::string::npos) {
                        std::string p_name = part.substr(0, eq);
                        std::string p_expr = part.substr(eq + 1);
                        p_name.erase(0, p_name.find_first_not_of(" \t"));
                        p_name.erase(p_name.find_last_not_of(" \t") + 1);
                        kwargs[p_name] = evaluate(p_expr, local_vars, global_vars, state);
                    }
                }
            }

            if (functions.count(func_name)) {
                std::string res = run_ast(functions[func_name], kwargs, global_vars, functions, state);
                if (res == "exitloop") return res;
            }
        }
        else if (line == "try") {
            last_try_failed = false;
            try {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (!res.empty()) return res;
            } catch (...) {
                last_try_failed = true;
            }
        }
        else if (line == "catch") {
            if (last_try_failed) {
                std::string res = run_ast(children, local_vars, global_vars, functions, state);
                if (!res.empty()) return res;
            }
        }
        else if (line.rfind("wait ", 0) == 0) {
            double sec = evaluate(line.substr(5), local_vars, global_vars, state).to_number();
            usleep(static_cast<useconds_t>(sec * 1000000));
        }
        else if (line.rfind("warn(", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string msg = line.substr(start, end - start);
            std::cerr << "WARNING: " << evaluate(msg, local_vars, global_vars, state).to_string() << "\n";
        }
        else if (line.rfind("critical(", 0) == 0) {
            size_t start = line.find('(') + 1;
            size_t end = line.rfind(')');
            std::string msg = line.substr(start, end - start);
            std::cerr << "CRITICAL: " << evaluate(msg, local_vars, global_vars, state).to_string() << "\n";
            std::exit(1);
        }
        else if (line == "exit") {
            std::exit(0);
        }
        else if (line == "exitloop") {
            return "exitloop";
        }
        else {
            // Extract the first identifier to check if it's a standalone library call
            std::string first_word = "";
            for (char c : line) {
                if (std::isalnum(c) || c == '_') first_word += c;
                else break;
            }
            
            if (state.libraries.count(first_word)) {
                // Evaluate as a standalone library execution
                evaluate(line, local_vars, global_vars, state);
            } else {
                throw std::runtime_error("Syntax Error: Unrecognized command: '" + line + "'");
            }
        }
    }
    return "";
}

// Interactive prompt for environment setup TUI
void run_environment_tui() {
    std::cout << "Ready to create an environment. Please enter the environment’s variables below:\n\n";
    std::string name, mem, cpu, blocked;
    std::cout << "NAME > ";
    std::getline(std::cin, name);
    if (name.empty()) {
        std::cerr << "Error: Environment name cannot be empty.\n";
        std::exit(1);
    }
    std::cout << "MAX-MEM-USAGE (MB) > ";
    std::getline(std::cin, mem);
    std::cout << "MAX-CPU-USAGE (%) > ";
    std::getline(std::cin, cpu);
    std::cout << "BLOCKED-STATEMENTS (comma-separated) > ";
    std::getline(std::cin, blocked);

    std::string filepath = get_env_directory() + "/" + name + ".json";
    std::ofstream f(filepath);
    if (f.is_open()) {
        f << "{\n"
          << "    \"name\": \"" << name << "\",\n"
          << "    \"max_mem_usage\": " << (mem.empty() ? "null" : mem) << ",\n"
          << "    \"max_cpu_usage\": " << (cpu.empty() ? "null" : cpu) << ",\n"
          << "    \"blocked_statements\": \"" << blocked << "\"\n"
          << "}\n";
        std::cout << "\nEnvironment creation is complete. You can now use \"" << name << "\" across Vex programs on your system.\n";
    } else {
        std::cerr << "Error: Failed to write environment configuration.\n";
        std::exit(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: vex <file.vex> OR vex env create OR vex create\n";
        return 1;
    }

    std::string first_arg = argv[1];
    bool is_env_creation = (first_arg == "create") || 
                           (first_arg == "env" && argc >= 3 && std::string(argv[2]) == "create");

    if (is_env_creation) {
        run_environment_tui();
        return 0;
    }

    std::string filepath = argv[1];
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        std::cerr << "Error: File '" << filepath << "' not found.\n";
        return 1;
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file '" << filepath << "'.\n";
        return 1;
    }

    std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::vector<std::string> lines = preprocess_code(code);
    std::vector<ASTNode> ast = parse_blocks(lines);

    ExecutionState state;
    ensure_default_main_env();
    apply_environment("main", state, false);

    pre_run_checks(ast, state);

    Scope global_vars;
    Scope local_vars;
    std::unordered_map<std::string, std::vector<ASTNode>> functions;

    try {
        run_ast(ast, local_vars, global_vars, functions, state);
    } catch (const std::exception& e) {
        std::cerr << "Fatal Runtime Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}