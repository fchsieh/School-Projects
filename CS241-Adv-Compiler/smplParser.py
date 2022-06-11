import smplAST as ast


class Parser:
    def __init__(self, lex):
        self.lex = lex

    def start_parser(self):
        # The return value will be an ASTree
        return self.computation()

    def peek(self):
        return self.lex.peek().token

    def consume(self, type):
        next = self.lex.next()
        if next and next.token == type:
            return next

        if not next:
            raise Exception(
                "[ERROR] POS: {}, Type: {}, Token: {} not consumed.".format(
                    self.lex.pos, type, self.lex.input[self.lex.pos]
                )
            )
        raise Exception(
            "[ERROR] Unexpected token: POS: {}, Type: {}, Token: {}.".format(
                self.lex.pos, type, self.lex.input[self.lex.pos]
            )
        )

    def relOp(self):
        op = self.peek()
        self.consume(self.peek())
        return op

    # ident = letter {letter | digit}
    def ident(self):
        letter = self.consume("IDENT")
        return ast.Identifier(letter.val)

    # number = digit {digit}
    def number(self):
        num = self.consume("NUMBER")
        return ast.Number(int(num.val))

    # designator = ident{ "[" expression "]" }
    def designator(self):
        ident = self.ident()
        arr = []
        while self.peek() == "LBRACKET":
            self.consume("LBRACKET")
            arr.append(self.expression())
            self.consume("RBRACKET")
        if not arr:
            return ident
        return ast.Array(ident, arr)

    # factor = designator | number | "(" expression ")" | funcCall
    def factor(self):
        res = None
        if self.peek() == "IDENT":
            return self.designator()
        if self.peek() == "NUMBER":
            return self.number()
        if self.peek() == "CALL":
            return self.func_call()
        if self.peek() == "LPAREN":
            self.consume("LPAREN")
            res = self.expression()
            self.consume("RPAREN")

        return res

    # term = factor { ("*" | "/") factor}
    def term(self):
        left = self.factor()
        while self.peek() in {"ASTERISK", "SLASH"}:
            op = "ASTERISK" if self.peek() == "ASTERISK" else "SLASH"
            self.consume(op)
            right = self.factor()
            left = ast.Operator(op, left, right)
        return left

    # expression = term {("+" | "-") term}
    def expression(self):
        left = self.term()
        while self.peek() in {"PLUS", "MINUS"}:
            op = "PLUS" if self.peek() == "PLUS" else "MINUS"
            self.consume(op)
            right = self.term()
            left = ast.Operator(op, left, right)
        return left

    # relation = expression relOp expression
    def relation(self):
        left = self.expression()
        op = self.relOp()
        right = self.expression()
        return ast.Operator(op, left, right)

    # assignment = "let" designator "<-" expression
    def assignment(self):
        self.consume("LET")
        left = self.designator()
        self.consume("ASSIGN")
        right = self.expression()
        return ast.Assignment(left, right)

    # funcCall = "call" ident [ "(" [expression { "," expression } ] ")" ]
    def func_call(self):
        self.consume("CALL")
        funcName = self.ident()
        params = []
        if self.peek() == "LPAREN":
            self.consume("LPAREN")
            if self.peek() in {"IDENT", "NUMBER", "LPAREN", "CALL"}:
                params.append(self.expression())
                while self.peek() == "COMMA":
                    self.consume("COMMA")
                    params.append(self.expression())
            self.consume("RPAREN")
        return ast.FuncCall(funcName, params)

    # ifStatement = "if" relation "then" statSequence [ "else" statSequence ] "fi"
    def if_stat(self):
        self.consume("IF")
        relation = self.relation()
        self.consume("THEN")
        thenStatement = self.stat_sequence()
        elseStatement = None
        if self.peek() == "ELSE":
            self.consume("ELSE")
            elseStatement = self.stat_sequence()
        self.consume("FI")
        if not elseStatement:
            elseStatement = []
        return ast.IfStatement(relation, thenStatement, elseStatement)

    # whileStatement = "while" relation "do" StatSequence "od"
    def while_stat(self):
        self.consume("WHILE")
        relation = self.relation()
        self.consume("DO")
        statement = self.stat_sequence()
        self.consume("OD")
        return ast.WhileStatement(relation, statement)

    # returnStatement = "return" [ expression ]
    def return_stat(self):
        self.consume("RETURN")
        # expression -> [ident, number, (expression), call]
        ret = None
        if self.peek() in {"IDENT", "NUMBER", "LPAREN", "CALL"}:
            ret = self.expression()

        return ast.ReturnStatement(ret)

    # statement = assignment | funcCall | ifStatement | whileStatement | returnStatemen
    def statement(self):
        if self.peek() == "LET":
            return self.assignment()
        if self.peek() == "CALL":
            return self.func_call()
        if self.peek() == "IF":
            return self.if_stat()
        if self.peek() == "WHILE":
            return self.while_stat()
        if self.peek() == "RETURN":
            return self.return_stat()

    # statSequence = statement { ";" statement } [ ";" ]
    def stat_sequence(self):
        statements = [self.statement()]
        while self.peek() == "SEMICOLON":
            self.consume("SEMICOLON")
            if self.peek() in {"LET", "CALL", "IF", "WHILE", "RETURN"}:
                statements.append(self.statement())

        return statements

    # typeDecl = "var" | "array" "[" number "]" { "[" number "]" }
    def type_decl(self):
        type = (
            "VAR" if self.peek() == "VAR" else "ARRAY"
        )  # get variable type (var or array)
        if type == "ARRAY":
            arr = []
            self.consume("ARRAY")
            self.consume("LBRACKET")
            arr.append(self.number())
            self.consume("RBRACKET")
            while self.peek() == "LBRACKET":
                self.consume("LBRACKET")
                arr.append(self.number())
                self.consume("RBRACKET")
            return arr
        else:
            self.consume("VAR")
            return []

    # varDecl = typeDecl ident { "," ident } ";"
    def var_decl(self):
        arr = self.type_decl()
        idents = [self.ident()]
        while self.peek() == "COMMA":
            self.consume("COMMA")
            idents.append(self.ident())
        self.consume("SEMICOLON")
        return [ast.VarDecl(ident, arr) for ident in idents]

    # funcDecl = [ "void" ] "function" ident formalParam ";" funcBody ";"
    def func_decl(self):
        is_void = False
        if self.peek() == "VOID":
            self.consume("VOID")
            is_void = True
        self.consume("FUNCTION")
        funcIdent = self.ident()
        params = self.formal_param()
        self.consume("SEMICOLON")
        body = self.func_body()
        self.consume("SEMICOLON")
        return ast.FuncDecl(
            func_name=funcIdent, params=params, body=body, is_void=is_void
        )

    # formalParam = "(" [ident { "," ident }] ")"
    def formal_param(self):
        self.consume("LPAREN")
        idents = []
        if self.peek() == "IDENT":
            idents.append(self.ident())
            while self.peek() == "COMMA":
                self.consume("COMMA")
                idents.append(self.ident())
        self.consume("RPAREN")
        return idents

    # funcBody = { varDecl } "{" [ statSequence ] "}"
    def func_body(self):
        varDecl = []
        stats = []
        while self.peek() in {"VAR", "ARRAY"}:
            varDecl.extend(self.var_decl())
        self.consume("LBRACE")
        if self.peek() in {"LET", "CALL", "IF", "WHILE", "RETURN"}:
            stats = self.stat_sequence()
        self.consume("RBRACE")
        return {"vars": varDecl, "stats": stats}

    # computation = "main" { varDecl } { funcDecl } "{" statSequence "}" "."
    def computation(self):
        varDecl = []
        funcDecl = []

        self.consume("MAIN")
        while self.peek() in {"VAR", "ARRAY"}:
            varDecl.extend(self.var_decl())
        while self.peek() in {"VOID", "FUNCTION"}:
            funcDecl.append(self.func_decl())

        self.consume("LBRACE")
        statements = self.stat_sequence()
        self.consume("RBRACE")
        self.consume("PERIOD")

        return ast.Computation(varDecl, funcDecl, statements)
