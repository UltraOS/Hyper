#!/usr/bin/python3
import argparse
import os

BYTES_PER_LINE = 8

def main():
    parser = argparse.ArgumentParser("Dump a binary file as a C array")
    parser.add_argument("input_file", type=str)
    parser.add_argument("output_file", type=str)
    parser.add_argument("variable_name", type=str,
                        help="Variable prefix to use for buffer & size")
    args = parser.parse_args()

    in_file = open(args.input_file, "rb")
    in_file_size = os.path.getsize(args.input_file)
    if not in_file_size:
        print(f"{args.input_file} is empty!")
        exit(1)

    with open(args.output_file, "w") as out_file: 
        out_file.write(f"unsigned long {args.variable_name}_size = {in_file_size};\n")
        out_file.write(f"unsigned char {args.variable_name}_data[] = ")
        out_file.write("{\n    ")

        for i in range(1, in_file_size + 1):
            byte = in_file.read(1)
            out_file.write(f"0x{byte.hex().upper()},")

            if i == in_file_size:
                break

            if (i % BYTES_PER_LINE) == 0:
                out_file.write("\n    ")
            else:
                out_file.write(" ")

        out_file.write("\n};\n")

if __name__ == "__main__":
    main()
