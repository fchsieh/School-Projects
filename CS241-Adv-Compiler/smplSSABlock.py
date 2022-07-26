import collections
import operator
from os import kill

import smplSSA


class Block:
    def __init__(self):
        self.label = -1
        self.instrs = []
        self.children = collections.defaultdict(set)
        self.local_variables = {}
        self.local_arr_strides = {}
        self.dominates = []
        self.search_list = collections.defaultdict(list)
        self.join_block_killed = []
        self.possibly_killed_load = {}

    def rename_op(self, old_op, new_op, visited=None):
        if not visited:
            visited = set()
        if self in visited:
            return
        visited.add(self)
        for instr in self.instrs:
            new_instr_ops = list(instr.ops)
            for i, op in enumerate(instr.ops):
                if op == old_op:
                    new_instr_ops[i] = new_op
            instr.ops = tuple(new_instr_ops)
        for _, succesors in self.children.items():
            # rename instr in the children that has the same name as this instr
            for child in succesors:
                child.rename_op(old_op, new_op, visited)

    def emit(self, instr_index, instr_name, *args, check_dup=True, is_empty=False):
        # Return: emit instruction and change of instruction count
        # Default: increase one after emitting
        # We need to return -1 if we don't want the instruction count to increase
        instr_change = 0

        if len(self.instrs) == 1 and self.instrs[0].is_empty:
            # this is an empty block, we need to delete it first,
            # then recreate it with empty instruction object
            del self.instrs[0]

        instr = smplSSA.Instruction(instr_name, *args)
        if instr_name == "adda":
            dom_list = self.search_list["load"]
        else:
            dom_list = self.search_list[instr_name]

        if not is_empty:
            if check_dup:  # perform CSE
                identical = instr.find_identical_instr(dom_list)
                if identical is not None:
                    # identical instruction found, no need to increase the instruction count
                    return smplSSA.InstructionOp(identical), instr_change - 1
                # Handle CSE for array
                # potentially ignored op: load (adda then load)
                elif (
                    # current is load
                    instr.instr == "load"
                    # load should be followed by an adda
                    and len(self.instrs) > 0
                    and self.instrs[-1].instr == "adda"
                    # should be loading the previous adda instr
                    and instr.ops[0] == smplSSA.InstructionOp(self.instrs[-1])
                ):
                    adda_before_load = self.instrs[-1]  # get adda before this load
                    orig_ops = instr.ops
                    start_check_dup = True
                    # first we check whether 'adda' is possibly killed by its predecessor
                    for killed_adda in self.possibly_killed_load:
                        if killed_adda.ops == adda_before_load.ops:
                            # we should skip this case, since it is killed already
                            start_check_dup = False
                            # clear search list and add this (adda,load) pair to it
                            self.search_list["load"] = [self.instrs[-1], instr]
                            # clear possibly killed load, since the first one is killed
                            self.possibly_killed_load = {}

                    if start_check_dup:
                        prev_identical_adda = adda_before_load.find_identical_instr(
                            dom_list
                        )
                        if prev_identical_adda is not None:
                            # load previous adda instr, check whether it already exists
                            # this is to make it consistent with other load instrs
                            instr.ops = (smplSSA.InstructionOp(prev_identical_adda),)
                            identical = instr.find_identical_instr(dom_list)
                            if identical is not None:
                                # Both load and adda can be eliminated
                                del self.instrs[-1]
                                # need to remove one more line of instr!
                                return (
                                    smplSSA.InstructionOp(identical),
                                    instr_change - 2,
                                )
                            else:
                                instr.ops = orig_ops
            # either perform cse or not, we still need to concatenate dom instr list
            if (
                instr_name == "store"
                and len(self.instrs) > 0
                and self.instrs[-1].instr == "adda"
            ):
                # 'adda' in this block will affect its child block
                # need to check whether ops are the same (if same, it should be reloaded)
                self.join_block_killed.append(self.instrs[-1])
                # add kill to load searchlist
                self.search_list["load"] = []
            elif instr_name == "adda":
                self.search_list["load"].append(instr)
            else:
                self.search_list[instr_name].append(instr)
            instr.i = instr_index
        elif is_empty:  # this is an '<empty>' instr
            instr.is_empty = True
            instr_change -= 1
        self.instrs.append(instr)
        return smplSSA.InstructionOp(instr), instr_change

    def add_child(self, type, block):
        self.children[type].add(block)

    def declare_local_var(self, name, strides=None):
        if name in self.local_variables.keys():
            raise Exception("[ERROR] Attempted to redeclare variable '{}'".format(name))
        self.local_variables[name] = None
        self.local_arr_strides[name] = strides

    def get_local_var(self, name):
        if name not in self.local_variables.keys():
            raise Exception(
                "[ERROR] Accessing an undeclared variable '{}'".format(name)
            )
        var = self.local_variables[name]
        strides = self.local_arr_strides[name]
        if not var:
            return smplSSA.ImmediateOp(name, un_init=True), strides
        return var, strides

    def set_local_var(self, name, val):
        if name not in self.local_variables.keys():
            raise Exception(
                "[ERROR] Accessing an undeclared variable '{}'".format(name)
            )
        self.local_variables[name] = val

    def block_links(self):
        children_keys = self.children.keys()
        links = {}
        # this is a ifStatement related block
        if (
            "then" in children_keys
            or "else" in children_keys
            or "join" in children_keys
        ):
            if "then" in children_keys and "else" in children_keys:
                links = {
                    "branch": self.children["else"],
                    "fall_through": self.children["then"],
                }
            elif "then" not in children_keys and "join" in children_keys:
                links = {"fall_through": self.children["join"]}

        # this is a whileStatement related block
        if (
            "head" in children_keys
            or "body" in children_keys
            or "exit" in children_keys
        ):
            if "head" in children_keys:
                links = {"fall_through": self.children["head"]}
            elif "body" in children_keys and "exit" in children_keys:
                links = {
                    "fall_through": self.children["body"],
                    "branch": self.children["exit"],
                }

        links["dom"] = self.dominates

        return links

    def constant_elimination(self):
        pyfuns = {
            "add": operator.add,
            "adda": operator.add,
            "div": operator.truediv,
            "sub": operator.sub,
            "mul": operator.mul,
        }
        # for removing unnecessary ops (ex: add 0, sub 0...)
        left_unit = {
            "add": smplSSA.ImmediateOp(0),
            "adda": smplSSA.ImmediateOp(0),
            "mul": smplSSA.ImmediateOp(1),
        }
        right_unit = {
            "add": smplSSA.ImmediateOp(0),
            "adda": smplSSA.ImmediateOp(0),
            "sub": smplSSA.ImmediateOp(0),
            "mul": smplSSA.ImmediateOp(1),
            "div": smplSSA.ImmediateOp(1),
        }

        n_eliminated = 1
        while n_eliminated > 0:  # need to keep iterating until we reach a fixed point
            n_eliminated = 0
            for i, instr in enumerate(self.instrs):
                opcode = instr.instr
                if opcode in pyfuns:
                    assert len(instr.ops) == 2
                    left, right = instr.ops
                    if isinstance(left, smplSSA.ImmediateOp) and isinstance(
                        right, smplSSA.ImmediateOp
                    ):
                        if left.un_init or right.un_init:
                            # skip this command, since it consists of uninit variables
                            continue
                        res = int(pyfuns[instr.instr](left.val, right.val))

                        # Delete instruction that should be eliminated
                        del self.instrs[i]
                        self.rename_op(
                            smplSSA.InstructionOp(instr), smplSSA.ImmediateOp(res)
                        )
                        n_eliminated += 1

                    elif opcode in left_unit and left == left_unit[opcode]:
                        del self.instrs[i]
                        self.rename_op(smplSSA.InstructionOp(instr), right)
                        n_eliminated += 1

                    elif opcode in right_unit and right == right_unit[opcode]:
                        del self.instrs[i]
                        self.rename_op(smplSSA.InstructionOp(instr), left)
                        n_eliminated += 1

    def build_instr_reorder_table(self, graph_reorder_table, block_start_i):
        # Get new instruction order
        original_instrs_order = []
        for instr in self.instrs:
            if instr.i > 0:  # ignore empty instructions
                original_instrs_order.append(instr.i)

        start_i = block_start_i
        end_i = start_i + len(original_instrs_order)
        new_instr_order = list(range(start_i, end_i))

        # Build global instr ordering table
        # Map old i -> new i
        for old, new in zip(original_instrs_order, new_instr_order):
            graph_reorder_table[old] = new

        # Return instr order for the next block to be processed
        if len(new_instr_order) > 0:
            next_block_start_i = new_instr_order[-1] + 1
        else:
            next_block_start_i = block_start_i
        return next_block_start_i
