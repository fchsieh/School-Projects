import re

INTEGER_SIZE = 4

RESERVED_TABLE = set(
    [
        "MAIN",
        "FUNCTION",
        "VOID",
        "VAR",
        "ARRAY",
        "RETURN",
        "CALL",
        "IF",
        "ELSE",
        "THEN",
        "FI",
        "WHILE",
        "DO",
        "OD",
        "LET",
    ]
)

TOKENS = [
    ["COMMA", ","],
    ["PERIOD", "."],
    ["SEMICOLON", ";"],
    ["MAIN", "main"],
    ["FUNCTION", "function"],
    ["VOID", "void"],
    ["VAR", "var"],
    ["ARRAY", "array"],
    ["RETURN", "return"],
    ["CALL", "call"],
    ["IF", "if"],
    ["THEN", "then"],
    ["ELSE", "else"],
    ["FI", "fi"],
    ["WHILE", "while"],
    ["DO", "do"],
    ["OD", "od"],
    ["LET", "let"],
    ["ASSIGN", "<-"],
    ["PLUS", "+"],
    ["MINUS", "-"],
    ["ASTERISK", "*"],
    ["SLASH", "/"],
    ["LPAREN", "("],
    ["RPAREN", ")"],
    ["LBRACKET", "["],
    ["RBRACKET", "]"],
    ["LBRACE", "{"],
    ["RBRACE", "}"],
    ["OP_GE", ">="],
    ["OP_LE", "<="],
    ["OP_NEQ", "!="],
    ["OP_EQ", "=="],
    ["OP_LT", "<"],
    ["OP_GT", ">"],
    ["NUMBER", re.compile(r"[0-9]+")],
    ["IDENT", re.compile(r"[a-zA-Z]([a-zA-Z0-9]+)?")],
]

OP_TABLE = {
    "PLUS": "add",
    "MINUS": "sub",
    "ASTERISK": "mul",
    "SLASH": "div",
    "OP_LT": "cmp",
    "OP_GT": "cmp",
    "OP_EQ": "cmp",
    "OP_NEQ": "cmp",
    "OP_GE": "cmp",
    "OP_LE": "cmp",
}


BUILTIN_FUNCS = {
    "InputNum": "read",
    "inputNum": "read",
    "OutputNum": "write",
    "outputNum": "write",
    "OutputNewLine": "writeNL",
    "outputNewLine": "writeNL",
}

BRANCH_OP_MAP = {
    "OP_GE": "blt",
    "OP_GT": "ble",
    "OP_LE": "bgt",
    "OP_LT": "bge",
    "OP_NEQ": "beq",
    "OP_EQ": "bne",
    "BRANCH": "bra",  # for output
}
