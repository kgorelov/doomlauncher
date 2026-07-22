#include "template_engine.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace doomlauncher {

namespace {

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

Value get_scope_val(const std::string& key, const Scope& scope) {
    std::string trimmed = trim(key);
    if (trimmed.length() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return Value(trimmed.substr(1, trimmed.length() - 2));
    }
    if (trimmed.length() >= 2 && trimmed.front() == '\'' && trimmed.back() == '\'') {
        return Value(trimmed.substr(1, trimmed.length() - 2));
    }
    auto it = scope.find(trimmed);
    if (it != scope.end()) {
        return it->second;
    }
    return Value();
}

Value get_operand_val(const std::string& key, const Scope& scope) {
    std::string trimmed = trim(key);
    if (trimmed.length() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return Value(trimmed.substr(1, trimmed.length() - 2));
    }
    if (trimmed.length() >= 2 && trimmed.front() == '\'' && trimmed.back() == '\'') {
        return Value(trimmed.substr(1, trimmed.length() - 2));
    }
    auto it = scope.find(trimmed);
    if (it != scope.end()) {
        return it->second;
    }
    return Value(trimmed);
}

bool eval_condition(const std::string& cond_raw, const Scope& scope) {
    std::string cond = trim(cond_raw);
    if (cond.empty()) return false;

    if (cond.rfind("not ", 0) == 0) {
        std::string sub = cond.substr(4);
        return !eval_condition(sub, scope);
    }

    size_t eq_pos = cond.find("==");
    if (eq_pos != std::string::npos) {
        std::string left = cond.substr(0, eq_pos);
        std::string right = cond.substr(eq_pos + 2);
        Value left_v = get_operand_val(left, scope);
        Value right_v = get_operand_val(right, scope);
        return left_v.to_string() == right_v.to_string();
    }

    size_t neq_pos = cond.find("!=");
    if (neq_pos != std::string::npos) {
        std::string left = cond.substr(0, eq_pos);
        std::string right = cond.substr(eq_pos + 2);
        Value left_v = get_operand_val(left, scope);
        Value right_v = get_operand_val(right, scope);
        return left_v.to_string() != right_v.to_string();
    }

    Value val = get_scope_val(cond, scope);
    return val.is_truthy();
}

enum class NodeType { Text, Variable, IfBlock, ForBlock };

struct ASTNode {
    NodeType type = NodeType::Text;
    std::string text;
    std::string var_name;
    std::string condition;
    std::string loop_var;
    std::string list_var;

    std::vector<ASTNode> children;
    std::vector<ASTNode> else_children;
};

std::string render_ast(const std::vector<ASTNode>& nodes, const Scope& scope);

std::string render_interpolated_text(const std::string& text, const Scope& scope) {
    std::string result;
    size_t i = 0;
    size_t len = text.length();

    while (i < len) {
        if (i + 1 < len && text[i] == '{' && text[i + 1] == '{') {
            size_t end = text.find("}}", i + 2);
            if (end != std::string::npos) {
                std::string var_name = trim(text.substr(i + 2, end - (i + 2)));
                auto it = scope.find(var_name);
                if (it != scope.end()) {
                    result += it->second.to_string();
                } else {
                    result += text.substr(i, end + 2 - i);
                }
                i = end + 2;
                continue;
            }
        }

        if (i + 1 < len && text[i] == '$' && text[i + 1] == '{') {
            size_t end = text.find('}', i + 2);
            if (end != std::string::npos) {
                std::string var_name = trim(text.substr(i + 2, end - (i + 2)));
                auto it = scope.find(var_name);
                if (it != scope.end()) {
                    result += it->second.to_string();
                } else {
                    result += text.substr(i, end + 1 - i);
                }
                i = end + 1;
                continue;
            }
        }

        if (text[i] == '$' && i + 1 < len && (std::isalnum(text[i + 1]) || text[i + 1] == '_')) {
            size_t start = i + 1;
            size_t var_len = 0;
            while (start + var_len < len && (std::isalnum(text[start + var_len]) || text[start + var_len] == '_')) {
                var_len++;
            }
            std::string var_name = text.substr(start, var_len);
            auto it = scope.find(var_name);
            if (it != scope.end()) {
                result += it->second.to_string();
            } else {
                result += text.substr(i, 1 + var_len);
            }
            i = start + var_len;
            continue;
        }

        result += text[i];
        i++;
    }

    return result;
}

std::vector<ASTNode> parse_template(const std::string& template_str, size_t& pos, const std::string& end_tag = "") {
    std::vector<ASTNode> nodes;
    size_t len = template_str.length();

    while (pos < len) {
        size_t block_start = template_str.find("{%", pos);
        if (block_start == std::string::npos) {
            if (pos < len) {
                ASTNode node;
                node.type = NodeType::Text;
                node.text = template_str.substr(pos);
                nodes.push_back(node);
                pos = len;
            }
            break;
        }

        if (block_start > pos) {
            ASTNode node;
            node.type = NodeType::Text;
            node.text = template_str.substr(pos, block_start - pos);
            nodes.push_back(node);
        }

        size_t block_end = template_str.find("%}", block_start + 2);
        if (block_end == std::string::npos) {
            ASTNode node;
            node.type = NodeType::Text;
            node.text = template_str.substr(block_start);
            nodes.push_back(node);
            pos = len;
            break;
        }

        std::string tag_content = trim(template_str.substr(block_start + 2, block_end - (block_start + 2)));
        pos = block_end + 2;

        if (!end_tag.empty() && (tag_content == end_tag || (end_tag == "endif" && tag_content == "else"))) {
            pos = block_start; // Let caller inspect tag
            break;
        }

        if (tag_content.rfind("if ", 0) == 0) {
            ASTNode if_node;
            if_node.type = NodeType::IfBlock;
            if_node.condition = trim(tag_content.substr(3));

            if_node.children = parse_template(template_str, pos, "endif");

            if (pos < len && template_str.find("{%", pos) == pos) {
                size_t end = template_str.find("%}", pos + 2);
                if (end != std::string::npos) {
                    std::string tag = trim(template_str.substr(pos + 2, end - (pos + 2)));
                    if (tag == "else") {
                        pos = end + 2;
                        if_node.else_children = parse_template(template_str, pos, "endif");
                    }
                }
            }

            if (pos < len && template_str.find("{%", pos) == pos) {
                size_t end = template_str.find("%}", pos + 2);
                if (end != std::string::npos) {
                    std::string tag = trim(template_str.substr(pos + 2, end - (pos + 2)));
                    if (tag == "endif") {
                        pos = end + 2;
                    }
                }
            }

            nodes.push_back(if_node);
        } else if (tag_content.rfind("for ", 0) == 0) {
            ASTNode for_node;
            for_node.type = NodeType::ForBlock;
            std::string body = trim(tag_content.substr(4));
            size_t in_pos = body.find(" in ");
            if (in_pos != std::string::npos) {
                for_node.loop_var = trim(body.substr(0, in_pos));
                for_node.list_var = trim(body.substr(in_pos + 4));

                for_node.children = parse_template(template_str, pos, "endfor");

                if (pos < len && template_str.find("{%", pos) == pos) {
                    size_t end = template_str.find("%}", pos + 2);
                    if (end != std::string::npos) {
                        std::string tag = trim(template_str.substr(pos + 2, end - (pos + 2)));
                        if (tag == "endfor") {
                            pos = end + 2;
                        }
                    }
                }
            }
            nodes.push_back(for_node);
        }
    }

    return nodes;
}

std::string render_ast(const std::vector<ASTNode>& nodes, const Scope& scope) {
    std::string result;
    for (const auto& node : nodes) {
        if (node.type == NodeType::Text) {
            result += render_interpolated_text(node.text, scope);
        } else if (node.type == NodeType::IfBlock) {
            bool cond_met = eval_condition(node.condition, scope);
            if (cond_met) {
                result += render_ast(node.children, scope);
            } else {
                result += render_ast(node.else_children, scope);
            }
        } else if (node.type == NodeType::ForBlock) {
            Value list_val = get_scope_val(node.list_var, scope);
            Scope local_scope = scope;

            if (list_val.type == Value::Type::Array) {
                for (const auto& item : list_val.arr_val) {
                    local_scope[node.loop_var] = item;
                    result += render_ast(node.children, local_scope);
                }
            } else if (list_val.is_truthy()) {
                local_scope[node.loop_var] = list_val;
                result += render_ast(node.children, local_scope);
            }
        }
    }
    return result;
}

} // namespace

std::string TemplateEngine::render(const std::string& template_str, const Scope& scope) {
    size_t pos = 0;
    std::vector<ASTNode> ast = parse_template(template_str, pos);
    return render_ast(ast, scope);
}

} // namespace doomlauncher
