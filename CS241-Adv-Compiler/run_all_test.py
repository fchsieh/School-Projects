import glob
import os
import subprocess


def run_test(path="examples", resfile="output.txt", ref=False):

    files = glob.glob("./{}/**/*.smpl".format(path), recursive=True)
    with open(resfile, "w") as output:
        for f in files:
            print("=" * 10 + f + "=" * 10)
            output.write("\n")
            if not ref:
                subprocess.run(
                    ["python", "./main.py", f, "--no-view", "--output-png"],
                    stdout=output,
                )
            else:
                subprocess.run(["python", "./ref/main.py", f], stdout=output)


def rename(path="examples"):
    files = glob.glob("./{}/**/*.smpl".format(path), recursive=True)
    counter = 1
    for f in files:
        file_path = os.path.dirname(f)
        orig_filename = os.path.basename(f)
        new_filename = "{}\{}_{}".format(file_path, counter, orig_filename)
        os.rename(f, new_filename)
        counter += 1


run_test(path="examples", resfile="output.txt", ref=False)
# rename()
