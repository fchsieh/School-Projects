import copy

import smplSSABlock


class Graph:
    # The root graph, consists of all IR nodes (functions and main)
    def __init__(self):
        self.graphs = []


class SubGraph:
    def __init__(self):
        self.block_counter = 0
        self.instr_counter = 1
        self.root = None  # the starting node of the graph
        self.current_block = None
        self.params = []  # for function printing
        self.is_void = False  # for function printing

    def emit(self, *args, check_dup=False, is_empty=False):
        result_op, decrease_i_count = self.current_block.emit(
            self.instr_counter, *args, check_dup=check_dup, is_empty=is_empty
        )
        self.instr_counter += 1 + decrease_i_count
        return result_op

    def get_new_block(self, root=False, add_counter=True, same_context=False):
        block = smplSSABlock.Block()
        if not block:
            raise Exception("[ERROR] Failed to get new block")
        if root:
            self.current_block = block
            self.root = block
        if same_context:  # for if and while loops
            block.local_variables = copy.copy(self.current_block.local_variables)
            block.local_arr_strides = copy.copy(self.current_block.local_arr_strides)
            block.search_list = copy.copy(self.current_block.search_list)
        if add_counter:
            self.block_counter += 1
        block.label = self.block_counter

        return block

    def set_current_block(self, block):
        self.current_block = block
