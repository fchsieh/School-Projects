import argparse
import os

import smplIR
import smplLex
import smplParser
import smplSSAGraph


def main():
    argparser = argparse.ArgumentParser(description="A SMPL IR Generator")
    argparser.add_argument("input")
    argparser.add_argument("-i", "--ir", dest="ir", default=True, action="store_true")
    argparser.add_argument(
        "-l", "--lex", dest="lex", default=False, action="store_true"
    )
    argparser.add_argument(
        "-nc", "--no-ce", dest="no_ce", default=True, action="store_false"
    )
    argparser.add_argument(
        "-nv", "--no-view", dest="no_view", default=False, action="store_true"
    )
    argparser.add_argument(
        "-p", "--output-png", dest="output_png", default=False, action="store_true"
    )

    args = argparser.parse_args()

    if not args.input:
        raise FileNotFoundError

    code = open(args.input, "r")
    base = os.path.basename(args.input)
    ext = ".png" if args.output_png else ".pdf"
    output_file = "./output/" + os.path.splitext(base)[0] + ext

    # remove leading and trailing whitespace in input code
    lexer = smplLex.Lexer(code.read().strip())
    parser = smplParser.Parser(lexer)

    if args.lex:
        lexer.iterate()
        code.close()
        return
    if args.ir:
        ast = parser.start_parser()
        dotgraph = smplSSAGraph.Graph()
        ast.compile(dotgraph)
        smplIR.Output(
            Graph=dotgraph,
            fn=output_file,
            view=args.no_view,
            output_png=args.output_png,
            constant_elimination=args.no_ce,
        )

    code.close()


if __name__ == "__main__":
    main()
