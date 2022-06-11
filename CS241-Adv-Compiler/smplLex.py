import re

import config


class Token:
    def __init__(self, token, val, pos):
        self.token = token
        self.val = val
        self.pos = pos
        self.toklen = len(val)


class Lexer:
    def __init__(self, inFile):
        self.input = inFile
        self.pos = 0
        self.currToken = None

    def iterate(self):
        print("=" * 26)
        template = "{0:^5}|{1:^11}|{2:^10}"
        print(template.format("Pos", "Type", "Token"))
        print("=" * 26)
        while self.pos < len(self.input):
            next = self.next()
            if next:
                print(template.format(next.pos, next.token, next.val))
            else:
                raise Exception("[ERROR] Lexer iteration failed.")
        print("=" * 26)

    def peek(self):
        next, _ = self.lexer()
        return next

    def next(self):
        nextTok, consumedLen = self.lexer()
        delimiter = {" ", "\n", "\t", "\r", "\a"}
        # skip whitespace
        while (
            self.pos + consumedLen < len(self.input)
            and self.input[self.pos + consumedLen] in delimiter
        ):
            # ignore white spaces
            consumedLen += 1
        self.pos += consumedLen
        return nextTok

    def lexer(self):
        remainStr = self.input[self.pos :]
        if not remainStr:
            return None, 0

        token, consumedLen = None, 0

        for tokenType, tok in config.TOKENS:
            tempToken, tempTokenLen = None, 0
            if isinstance(tok, re.Pattern):
                match = tok.match(remainStr)
                if match:
                    tempToken = Token(tokenType, match.group(0), self.pos)
                    tempTokenLen = match.end(0)
                else:
                    # skip this token
                    continue
            else:
                if remainStr.startswith(tok):
                    tempToken = Token(tokenType, tok, self.pos)
                    tempTokenLen = len(tok)
                else:
                    # skip this token
                    continue

            if token is not None and tempTokenLen > token.toklen:
                if tempToken.val.upper() not in config.RESERVED_TABLE:
                    token = tempToken
                    consumedLen = tempTokenLen
            elif token is None:
                token = tempToken
                consumedLen = tempTokenLen

        return token, consumedLen
