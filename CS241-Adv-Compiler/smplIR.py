import graphviz

import smplSSA


class DotGraph:
    def __init__(self, graph, id, graph_label=None, graph_name=""):
        self.Graph = graph
        self.Block = graph.root
        self.id = id
        self.graph_label = graph_label
        self.block_labels = []
        self.block_offset = 0
        self.uninit_vars = set()
        self.block_label_table = {}  # orig label -> printout label
        self.graph_name = graph_name
        # for fixing instruction order
        self.graph_reorder_table = {}  # orig instr# -> printout instr#

    def set_block_offset(self, offset):
        # Change original block offset for ordering purposes
        self.block_offset = offset

    def check_uninit_var(self, instrs):
        # Find uninitialized variables for printing warning messages
        for instr in instrs:
            for op in instr.ops:
                if isinstance(op, smplSSA.ImmediateOp) and op.un_init:
                    self.uninit_vars.add(op.name)

    def print_uninit_var(self):
        for warn in self.uninit_vars:
            print(
                "[WARNING] [{}] Accessing an uninitialized variable '{}'".format(
                    self.graph_name, warn
                )
            )

    def fix_block_labels_order(self):
        keys = self.block_labels
        for i in range(len(keys)):
            self.block_label_table[keys[i]] = i + 1
        return self.block_labels

    def instr_reorder(self, block):
        reorder_table_keys = self.graph_reorder_table.keys()
        for instr in block.instrs:
            if instr.i in reorder_table_keys and not instr.has_reordered:
                instr.i = self.graph_reorder_table[instr.i]
                instr.has_reordered = True

            # reorder arguments
            for arg in instr.ops:
                if isinstance(arg, smplSSA.InstructionOp):
                    if arg.i in reorder_table_keys and not arg.has_reordered:
                        arg.i = self.graph_reorder_table[arg.i]
                        arg.has_reordered = True

    def build_graph(self, constant_elimination=True):
        block_stack = set([self.Block])
        block_table = {}  # The table that will be used for outputing the result

        while block_stack:  # Using DFS to print each block
            # A new block found!
            block = block_stack.pop()

            # Do constant elimination
            if constant_elimination:
                block.constant_elimination()

            # No instruction in this block, insert empty placement
            if len(block.instrs) == 0:
                self.Graph.set_current_block(block)
                self.Graph.emit("\<empty\>", is_empty=True)

            # Add this block to the table, so that we can process it later
            block_table[block.label] = block

            for _, children in block.children.items():
                # DFS, add children to the stack
                for child in children:
                    if child.label not in block_table.keys():
                        block_stack.add(child)

        defines = []
        connection = []
        # Start parsing block table by block label
        self.block_labels = sorted(block_table.keys())

        # Reorder the block labels and instruction order
        block_labels = self.fix_block_labels_order()

        # Reorder instructions
        if constant_elimination:
            instr_reorder_i = 1
            for block_idx in block_labels:
                block = block_table[block_idx]
                instr_reorder_i = block.build_instr_reorder_table(
                    self.graph_reorder_table, instr_reorder_i
                )

        # Reorder block labels
        for block_idx in block_labels:
            block = block_table[block_idx]

            # Start reordering for the instructions in this block (if constant elimination is enabled)
            if constant_elimination:
                self.instr_reorder(block)

            # get instrs of the block
            block_instrs = list(map(str, block.instrs))

            # print warning if uninitialized variables found
            self.check_uninit_var(block.instrs)

            # Create connections between blocks that have dependencies
            block_links = block.block_links()

            if "branch" in block_links.keys() and block_links["branch"]:
                child = block_links["branch"]
                for c in child:
                    connection.append(
                        '\t\tbb{} -> bb{} [label="{}"];'.format(
                            block.label + self.block_offset,
                            c.label + self.block_offset,
                            "branch",
                        )
                    )

            if "fall_through" in block_links and block_links["fall_through"]:
                child = block_links["fall_through"]
                for c in child:
                    connection.append(
                        '\t\tbb{} -> bb{} [label="{}"];'.format(
                            block.label + self.block_offset,
                            c.label + self.block_offset,
                            "fall-through",
                        )
                    )

            if "dom" in block_links and block_links["dom"]:
                child = block_links["dom"]
                for c in child:
                    connection.append(
                        '\t\tbb{} -> bb{} [label="{}", color="blue", style="dotted"];'.format(
                            block.label + self.block_offset,
                            c.label + self.block_offset,
                            "dom",
                        )
                    )

            dotgraph_block_code = (
                '\t\tbb{} [shape=record, label="<b>BB{}| {{{}}}"];'.format(
                    block.label + self.block_offset,
                    self.block_label_table[block.label],
                    "|".join(block_instrs),
                )
            )
            defines.append(dotgraph_block_code)

        graph_label = ""
        if self.graph_label is not None:
            graph_label = self.graph_label
        code = '\tsubgraph cluster_{} {{\n{}\n{}\n\t\tlabel="{}"\n\t}}'.format(
            self.id,
            "\n".join(defines),
            "\n".join(connection),
            graph_label,
        )
        num_block_labels = block_labels[-1]

        # print warning message for uninitialized variables
        self.print_uninit_var()

        return code, num_block_labels


def Output(Graph=None, fn="", view=True, output_png=False, constant_elimination=True):
    if not Graph:
        raise ValueError("Graph object not specified")
    id = 0
    # Create subgraph list from Root graph
    subgraphs = Graph.graphs
    # Draw root
    root_label = "main"
    root_graph = DotGraph(
        graph=subgraphs[0],
        id=id,
        graph_label=root_label,
        graph_name="main",
    )
    output_list = [root_graph]
    # Draw functions
    for g in subgraphs[1:]:
        id += 1
        func = g.root
        graph_name = func.name
        graph_label = ""
        if g.is_void:
            graph_label = "void "
        graph_label += func.name + " ({})".format(", ".join(g.params))
        dotgraph = DotGraph(
            graph=g, id=id, graph_label=graph_label, graph_name=graph_name
        )
        output_list.append(dotgraph)

    block_codes = []
    graph_offset = 0

    # Draw other blocks in the main
    for graph in output_list:
        graph.set_block_offset(graph_offset)
        code, block_offset = graph.build_graph(
            constant_elimination=constant_elimination
        )
        graph_offset += block_offset
        block_codes.append(code)

    output = "digraph G {{\n{}\n}}".format("\n".join(block_codes))
    print(output)
    # output to file
    with open(fn, "w") as f:
        f.write(output)

    # generate dot graph with pydot
    outformat = "png" if output_png else "pdf"

    src = graphviz.Source(output)
    src.render(engine="dot", format=outformat, outfile=fn, view=(not view))
