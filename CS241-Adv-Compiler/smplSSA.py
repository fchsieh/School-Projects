class InstructionOp:
    def __init__(self, instr):
        self.i = instr.i
        # for instruction reordering after constant elimination
        self.has_reordered = False

    def __str__(self):
        return "({})".format(self.i)

    def __eq__(self, other):
        return isinstance(other, InstructionOp) and self.i == other.i


class ImmediateOp:
    def __init__(self, val, un_init=False):
        self.val = val
        self.un_init = un_init
        self.name = ""  # for printing uninit variables in warning
        if self.un_init:
            self.name = val
            self.val = 0

    def __str__(self):
        return "#{}".format(self.val)

    def __eq__(self, other):
        return isinstance(other, ImmediateOp) and self.val == other.val


class ArgumentOp:
    def __init__(self, ident):
        self.name = ident.name

    def __str__(self):
        return "@{}".format(self.name)


class Instruction:
    # Instruction object consists of instr and ops (InstructionOp, ImmediateOp, and ArgumentOp)
    def __init__(self, instr, *ops):
        self.instr = instr
        self.ops = ops
        self.i = -1
        self.dom_by_instr = None
        self.is_empty = False
        # for instruction reordering after constant elimination
        self.has_reordered = False

    def __str__(self):
        try:  # more than 1 arguments
            args = " ".join(str(op) for op in self.ops)
        except:  # (for load op)
            args = self.ops
        if not self.is_empty:
            return "{}: {} {}".format(self.i, self.instr, args)
        else:
            return "{}".format(self.instr)

    def find_identical_instr(self, dom_list):
        for candidate in dom_list:
            if candidate.ops == self.ops:
                return candidate
