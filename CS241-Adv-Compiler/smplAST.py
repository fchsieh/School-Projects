import config
import smplSSA
import smplSSAGraph


class Identifier:
    def __init__(self, name):
        self.name = name

    def compile(self, SSAGraph):
        valOp, _ = SSAGraph.current_block.get_local_var(self.name)
        return valOp


class Number:
    def __init__(self, val):
        self.val = val

    def compile(self, SSAGraph=None):
        return smplSSA.ImmediateOp(self.val)


class Array:  # from vid 11
    def __init__(self, ident, arr):
        self.ident = ident
        self.array = arr

    def compile(self, SSAGraph):
        addr_op = self.compile_addr(SSAGraph)
        load_op = SSAGraph.emit("load", addr_op, check_dup=True)
        return load_op

    def compile_addr(self, SSAGraph):
        name = self.ident.name
        base_addr, strides = SSAGraph.current_block.get_local_var(name)
        offset_op = smplSSA.ImmediateOp(0)
        # dot product of indices of an element and the strides provides the offset to that element in the buffer
        # https://docs.microsoft.com/en-us/windows/ai/directml/dml-strides
        for i, idx in enumerate(self.array):
            idx_op = idx.compile(SSAGraph)
            this_offset_op = SSAGraph.emit(
                "mul",
                idx_op,
                smplSSA.ImmediateOp(strides[i]),
                check_dup=True,
            )
            offset_op = SSAGraph.emit("add", offset_op, this_offset_op, check_dup=True)
        offset_op = SSAGraph.emit(
            "mul",
            offset_op,
            smplSSA.ImmediateOp(config.INTEGER_SIZE),
            check_dup=True,
        )
        # add FP base_addr
        # we do not check duplicate for adda here
        # if needed, when emitting "load", "adda" might be deleted
        addr_op = SSAGraph.emit("adda", offset_op, base_addr, check_dup=False)

        return addr_op


class VarDecl:
    def __init__(self, ident, dims):
        self.ident = ident
        self.dims = dims

    def compile(self, SSAGraph):
        name = self.ident.name
        dims = self.dims
        if dims:  # this variable is an array
            dims = [dim.val for dim in dims]
            strides = []
            for i in range(len(dims)):
                stride = 1
                for d in dims[i + 1 :]:
                    stride *= d
                strides.append(stride)

            SSAGraph.current_block.declare_local_var(name, strides)
            # start allocation for the current array
            size = 1
            for d in dims:
                size *= d
            base_addr = SSAGraph.emit(
                "alloca", smplSSA.ImmediateOp(size * config.INTEGER_SIZE)
            )
            SSAGraph.current_block.set_local_var(name, base_addr)
        else:
            SSAGraph.current_block.declare_local_var(name)
        return None


class FuncDecl:
    def __init__(self, func_name, params, body, is_void):
        self.ident = func_name
        self.params = params
        self.varDecls = body["vars"]
        self.statements = body["stats"]
        self.is_void = is_void

    def compile(self, GlobalGraph):
        FuncGraph = smplSSAGraph.SubGraph()
        new_root = FuncGraph.get_new_block(root=True)
        FuncGraph.set_current_block(new_root)
        FuncGraph.current_block.name = self.ident.name
        FuncGraph.current_block.arg_names = [ident.name for ident in self.params]

        FuncGraph.set_current_block(new_root)

        for param in self.params:
            FuncGraph.current_block.declare_local_var(param.name, None)
            FuncGraph.current_block.set_local_var(param.name, smplSSA.ArgumentOp(param))

        for var in self.varDecls:
            if var:
                var.compile(FuncGraph)

        for stmt in self.statements:
            if stmt:
                stmt.compile(FuncGraph)
        if self.is_void:
            FuncGraph.is_void = True
        FuncGraph.params = [ident.name for ident in self.params]
        GlobalGraph.graphs.append(FuncGraph)


class Computation:
    def __init__(self, varDecls, funcDecls, statements):
        self.varDecls = varDecls
        self.funcDecls = funcDecls
        self.statements = statements

    def compile(self, GlobalGraph):
        SSAGraph = smplSSAGraph.SubGraph()
        GlobalGraph.graphs.append(SSAGraph)

        root = SSAGraph.get_new_block(root=True)
        SSAGraph.set_current_block(root)

        # declare functions
        for fdecl in self.funcDecls:
            if fdecl:
                fdecl.compile(GlobalGraph)
        # declare vars
        for vdecl in self.varDecls:
            if vdecl:
                vdecl.compile(SSAGraph)
        # compile statements
        for stmt in self.statements:
            if stmt:  # prevent empty statements
                stmt.compile(SSAGraph)

        SSAGraph.arg_names = [param.ident.name for param in self.varDecls]
        SSAGraph.emit("end")
        return None


class Operator:
    def __init__(self, op, left, right):
        self.op = op
        self.left = left
        self.right = right

    def compile(self, SSAGraph):
        left = self.left.compile(SSAGraph)
        right = self.right.compile(SSAGraph)
        result = SSAGraph.emit(config.OP_TABLE[self.op], left, right, check_dup=True)
        return result

    def compile_conditional_jump(self, SSAGraph, join_block):
        cond_op = self.compile(SSAGraph)
        SSAGraph.emit(
            config.BRANCH_OP_MAP[self.op], cond_op, "(BB%s)" % join_block.label
        )
        return None


class Assignment:
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def compile(self, SSAGraph):
        val = self.right.compile(SSAGraph)
        if isinstance(self.left, Identifier):
            name = self.left.name
            if name not in SSAGraph.current_block.local_variables:
                raise Exception("[ERROR] Assigning an undeclared variable '%s'" % name)
            SSAGraph.current_block.set_local_var(self.left.name, val)

        elif isinstance(self.left, Array):
            name = self.left.ident.name
            if name not in SSAGraph.current_block.local_variables:
                raise Exception("[ERROR] Assigning an undeclared array '%s'" % name)

            addr_op = self.left.compile_addr(SSAGraph)
            SSAGraph.emit("store", val, addr_op)

        return None


class FuncCall:
    def __init__(self, ident, args):
        self.ident = ident
        self.args = args

    def compile(self, SSAGraph):
        params = []
        for param_expr in self.args:
            if param_expr is not None:
                param_op = param_expr.compile(SSAGraph)
                params.append(param_op)
        func_name = self.ident.name
        if func_name in config.BUILTIN_FUNCS.keys():
            func_name = config.BUILTIN_FUNCS[func_name]
            res_op = SSAGraph.emit("", func_name, *params, check_dup=False)
        else:
            res_op = SSAGraph.emit("call", func_name, *params, check_dup=False)
        return res_op


class IfStatement:
    def __init__(self, relation, thenStatement, elseStatement):
        self.relation = relation
        self.thenStatement = thenStatement
        self.elseStatement = elseStatement

    def compile(self, SSAGraph):
        # at join block, if load (ops) is in this list,
        # it should be regenerated (should not be eliminated!)
        possibly_killed_load = []
        # create child nodes with same context of the current node (vars, arrays...)
        then_block = SSAGraph.get_new_block(same_context=True)
        else_block = SSAGraph.get_new_block(same_context=True)
        join_block = SSAGraph.get_new_block(same_context=True)
        # add child nodes to the current node
        SSAGraph.current_block.add_child("then", then_block)
        SSAGraph.current_block.add_child("else", else_block)
        # three new nodes were dominiated by the current node
        SSAGraph.current_block.dominates.extend([then_block, else_block, join_block])
        # create branch instruction of the current node
        self.relation.compile_conditional_jump(SSAGraph, else_block)

        # Compile "then" block
        SSAGraph.set_current_block(then_block)
        for stmt in self.thenStatement:
            if stmt:
                stmt.compile(SSAGraph)
        if len(then_block.instrs) == 0:
            SSAGraph.emit("\<empty\>", is_empty=True)
        SSAGraph.emit("bra", "(BB%s)" % join_block.label)
        # Add join block for then/else, which should be fall through this join block!
        SSAGraph.current_block.add_child("join", join_block)
        if SSAGraph.current_block.join_block_killed:
            possibly_killed_load.extend(list(SSAGraph.current_block.join_block_killed))
        then_block = SSAGraph.current_block

        # Compile "else" block
        SSAGraph.set_current_block(else_block)
        for stmt in self.elseStatement:
            if stmt:
                stmt.compile(SSAGraph)
        if len(else_block.instrs) == 0:
            SSAGraph.emit("\<empty\>", is_empty=True)
        SSAGraph.emit("bra", "(BB%s)" % join_block.label)
        # Add join block for then/else, which should be fall through this join block!
        SSAGraph.current_block.add_child("join", join_block)
        if SSAGraph.current_block.join_block_killed:
            possibly_killed_load.extend(list(SSAGraph.current_block.join_block_killed))
        else_block = SSAGraph.current_block

        # Compile "join" block
        SSAGraph.set_current_block(join_block)
        SSAGraph.current_block.possibly_killed_load = set(possibly_killed_load)
        for name in SSAGraph.current_block.local_variables:
            val_a, _ = then_block.get_local_var(name)
            val_b, _ = else_block.get_local_var(name)
            if val_a == val_b:
                # do not need to use phi function here (val from left and right are the same)
                continue
            phi_op = SSAGraph.emit("phi", val_a, val_b)
            # change the variable the the new value (phi)
            SSAGraph.current_block.set_local_var(name, phi_op)

        return None


class WhileStatement:
    def __init__(self, relation, statement):
        self.relation = relation
        self.statement = statement

    def compile(self, SSAGraph):
        # at exit block, if load (ops) is in this list,
        # it should be regenerated (should not be eliminated!)
        possibly_killed_load = []
        # add starting block for the while statement
        head_block = SSAGraph.get_new_block(same_context=True)
        SSAGraph.current_block.dominates.append(head_block)
        SSAGraph.current_block.add_child("head", head_block)

        # Compile statements in the body block first (using a temporary block)
        # since variable might be changed in the body block
        old_instr_counter = SSAGraph.instr_counter
        tmp_body_block = SSAGraph.get_new_block(add_counter=False, same_context=True)
        SSAGraph.set_current_block(tmp_body_block)
        for stmt in self.statement:
            if stmt:
                stmt.compile(SSAGraph)
        tmp_body_block = SSAGraph.current_block

        # Compile head block, need to check whether we need a phi function
        # value might came from the body block (using tmp block here)
        # ps. head block only handles conditions (cmp and branch!)
        SSAGraph.set_current_block(head_block)
        for name in SSAGraph.current_block.local_variables:
            val_a, _ = SSAGraph.current_block.get_local_var(name)
            # from the body block (fall through block)
            val_b, _ = tmp_body_block.get_local_var(name)
            if val_a != val_b:
                phi_op = SSAGraph.emit("phi", val_a, val_b)
                SSAGraph.current_block.set_local_var(name, phi_op)

        # (in head) compile cond jump for exit block,
        # it will be done later after finishing this while stat
        exit_block = SSAGraph.get_new_block(same_context=True)
        self.relation.compile_conditional_jump(SSAGraph, exit_block)

        # Create the real body block
        new_instr_counter = SSAGraph.instr_counter
        body_block = SSAGraph.get_new_block(same_context=True)
        SSAGraph.instr_counter = old_instr_counter
        SSAGraph.set_current_block(body_block)
        for stmt in self.statement:
            if stmt:
                stmt.compile(SSAGraph)
        if SSAGraph.current_block.join_block_killed:
            possibly_killed_load.extend(list(SSAGraph.current_block.join_block_killed))
        SSAGraph.instr_counter = new_instr_counter
        SSAGraph.emit("bra", "(BB%s)" % head_block.label)

        # Set block links
        head_block.add_child("body", body_block)
        SSAGraph.current_block.add_child(
            "head", head_block
        )  # this is because of the looping
        head_block.add_child("exit", exit_block)
        head_block.dominates.append(body_block)
        head_block.dominates.append(exit_block)
        SSAGraph.set_current_block(exit_block)
        # append possible killed adda's to exit block
        SSAGraph.current_block.possibly_killed_load = set(possibly_killed_load)

        # body block might be empty
        if len(body_block.instrs) == 0:
            SSAGraph.emit("\<empty\>", is_empty=True)

        return None


class ReturnStatement:
    def __init__(self, val=None):
        self.val = val

    def compile(self, SSAGraph):
        ret = None
        if self.val:
            ret = self.val.compile(SSAGraph)
            SSAGraph.emit("return", ret)
        else:
            SSAGraph.emit("return", "")
        return ret
