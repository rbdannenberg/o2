# o2idc.py -- Interface Description Compiler for O2 messages
#
# Roger B. Dannenberg
# Dec 2021

# This preprocessor for C++ helps to build handlers for O2 messages
# with a fixed number of parameters with known types. The
# preprocessing automates the unpacking of message parameters, which
# are assigned to local variables as if the handler was called with a
# conventional parameter list instead of a message holding the
# parameters.
#
# Before each handler implementation (in a .cpp file), write an
# interface description beginning with:
# /* O2 INTERFACE: /service/node1/node2/node3 ...
# where "O2 INTERFACE" can also be "O2SM INTERFACE" or 
# "O2LITE INTERFACE" -- pick the one that applies. In this example, we
# write "service", "node1", "node2", "node3" as generic names, but
# typically they would be specific names like "xysensor" and "x".
#
# This is followed by parameter declarations of the form 
#     int32 id, float period, ...; an optional multi-line comment */
# where types are int32, float, int64, double, string, or bool
# (no other types are currently supported, but probably easy to add).
# 
# Note that the declaration ends with semicolon (;) and everything
# from there to the end-of-comment (*/) is ignored. The line after
# end-of-comment is ignored by this pre-processor.
#
# On the next line, begin the handler, which should look like:
#    void service_node1_node2_node3(O2_HANDLER_ARGS)
#    {
#        // begin unpack message (machine-generated):
#        // end unpack message
#        ...  message parameters id and period can be used here ...
#    }
# You should use O2SM_HANDLER_ARGS or O2LITE_HANDLER_ARGS in place
# of O2_HANDLER_ARGS if it applies.
# When this preprocessor is run, lines are inserted between the
# "// begin" and "// end" lines to extract parameters, which will
# be named according to the interface description, e.g. "id" and 
# "period" in this example. Use the parameters in the handler
# implementation. Parameters declared with type "string" are of
# C++ type "char *" and are not copied from the message, so the
# address becomes invalid after the handler returns. (You can copy
# the string if you need to retain it.)
#
# To activate these handlers, you need to call o2_method_new() (or
# o2sm_method_new() or o2l_method_new()) to register the handlers
# with O2 (or O2-shared-memory, O2Lite). This is accomplished at the
# end of the file by writing:
#
#    static void service_init()
#    {
#        // O2 INTERFACE INITIALIZATION: (machine generated)
#        // END INTERFACE INITIALIZATION
#    }
#
# Name this function with an actual service name, e.g. xysensor_init().
# This function will be populated with calls to o2_method_new() for
# all handlers declared previously in the file. Use O2SM or O2LITE if
# applicable.
# 
# You must call this _init() function once after O2 is initialized to 
# install the handlers.
#
# The preprocessor overwrites the .cpp file and is idempotent, meaning
# you can run the preprocessor again without damage.

import sys
import os

typecodes = {"string": "s", "int32": "i", "int64": "h", "float": "f",
             "double": "d", "bool": "B", "blob": "b"}


def pprint(outf, fn, strings):
    """
    Print a function call on outf with no separator.
    fn - the indentation and function name (without open paren)
    params - strings representing each parameter, with no commas or spaces
    The function call is wrapped and indented (at least we try to look nice).
    """
    params_col = len(fn) + 1  # includes "("
    # print a space before each parameter
    print(fn, "(", strings[0], file=outf, sep="", end="")
    col = params_col + len(strings[0])
    for i in range(1, len(strings)):
        print(",", file=outf, sep="", end="")
        col += 1
        if col + 1 + len(strings[i]) + 2 >= 78:  # 2 for ");"
            print(file=outf)  # newline
            # indent to params_col - 1 because we're about to print space
            print(" " * (params_col - 1), file=outf, sep="", end="")  # indent
            col = params_col - 1
        print(" ", strings[i], file=outf, sep="", end="")
        col += 1 + len(strings[i])
    print(");", file=outf, sep="")


def write_initialize(outf, o2id_type, methods):
    """write o2[l]_method_new calls for methods, which is a list 
    of (address, function, typestring) tuples
    """
    print("    //", o2id_type.upper(), "INTERFACE INITIALIZATION:",
          "(machine generated)", file=outf)
    for meth in methods:
        if o2id_type == 'o2lite':
            pprint(outf, "    o2l_method_new", ['"' + meth[0] + '"',
                   '"' + meth[2] + '"', "true", meth[1], "NULL"])
        else:
            pprint(outf, "    " + o2id_type + "_method_new",
                   ['"' + meth[0] + '"', '"' + meth[2] + '"', meth[1],
                    "NULL", "true", "true"])
        # show progress on stdout:
        print("       ", meth[0], '"' + meth[2] + '"', meth[1])
    print("    // END INTERFACE INITIALIZATION", file=outf)


def unpack_args(outf, o2id_type, description, handler, methods):
    typestring = ""
    # extract up to ";", gets whole string if no ";"
    description = description.split(";")
    if len(description) < 1:
        print("o2idc: got empty interface specification")
        return False
    description = description[0]
    description = description.split(",")  # split type name pairs
    # if there are no parameters at all, do not generate unpack code
    fields = description[0].split()
    if len(fields) < 1:
        print("o2idc: did not find address after INTERFACE:")
        return False
    address = fields[0]
    if len(fields) == 1:  # special case: no parameters
        methods.append((address, handler, ""))
        return True
    # remove address from first parameter:
    description[0] = description[0].strip()[len(address):]
    # now we just have a list of 1 or more parameter descriptions
    print("    // begin unpack message (machine-generated):", file=outf)
    for i, param in enumerate(description):
        fields = param.split()
        if len(fields) != 2:
            print("o2idc: could not parse parameter near", fields)
            print('    expected <type> <name>. Check for valid type ("int" is')
            print('    not valid, but "int32" is. Check for comma separating')
            print('    <type> <name> pairs. Check for terminating ";".')
            return False
        typ = fields[0]
        pname = fields[1]
        print("    ", file=outf, end='')
        if typ == "string":
            ctype = "char *"
        elif typ == "int32":
            ctype = "int32_t "
        elif typ == "int64":
            ctype = "int64_t "
        elif typ == "blob":
            ctype = "O2blob_ptr "
        else:
            ctype = (typ + " ")
        typecode = typecodes.get(typ)
        if not typecode:
            print("o2idc: invalid type name \"" + typ + "\" near", fields)
            if typ == "int":
                print("    Did you mean to use O2 type int32 or int64?")
            return False
        typestring = typestring + typecode
        if o2id_type == 'o2lite':
            print(ctype, pname, " = GET_", typ.upper(), "();",
                      sep='', file=hfile)
        else:
            print(ctype, pname, " = argv[", i, "]->", typecodes[typ], ";",
                  sep='', file=outf)
    methods.append((address, handler, typestring))
    print("    // end unpack message", file=outf)
    print("", file=outf)  # blank line to separate machine generated unpack
    return True


def process(filename, output_filename):
    """rewrite filename to output_filename, replacing generated code sections
    """
    print("   ", filename)
    wrote_initialize = False
    methods = []  # list of (address, handler, typestring) pairs
    outf = open(output_filename, "w")
    o2id_type = None
    with open(filename, "r") as file:
        state = 'findid'
        description = ""
        handler = ""
        while True:
            orig_line = file.readline()
            if len(orig_line) == 0:  # end-of-file condition
                break
            line = orig_line.strip()
            if state == 'findid' or state == 'skipandfindid':
                # if skipandfindid and we have a blank line, skip it
                if state == 'skipandfindid':
                    if len(line) == 0:
                        continue  # skip blank line
                    else:  # fall through and act as state findid
                        state = 'findid'
                if line.find("O2 INTERFACE:") >= 0 or \
                   line.find("O2LITE INTERFACE:") >= 0 or \
                   line.find("O2SM INTERFACE:") >= 0:
                    description = line[line.find("INTERFACE:") + 10:]
                    if wrote_initialize:
                        print("o2_idc: Message description must come before" +
                              " interface intialization")
                        print("    Description is:", description)
                        return False
                    state = 'gatherlines'
                    if line.find("O2LITE") >= 0:
                        o2id_type = 'o2lite'
                    elif line.find("O2SM") >= 0:
                        o2id_type = 'o2sm'
                    else:
                        o2id_type = 'o2'
                    if description.find("*/") >= 0:
                        state = 'findhandler'  # we have the whole spec
                elif line.find("O2 INTERFACE INITIALIZATION:") >= 0 or \
                     line.find("O2LITE INTERFACE INITIALIZATION:") >= 0 or \
                     line.find("O2SM INTERFACE INITIALIZATION:") >= 0:
                     state = 'skipinit'
                     continue  # do not write this out; we'll regenerate it
                outf.write(orig_line)
            elif len(line) == 0:
                if state == 'gatherlines': 
                    # blank line ends parameters even without semicolon
                    state = 'findhandler'
                if state != 'skipargs':
                    outf.write(orig_line)
                # otherwise no blank lines after function's open brace
            elif state == 'gatherlines':
                description = description + " " + line
                outf.write(orig_line)
                if line.find("*/") >= 0:
                    state = 'findhandler'  # we have the whole spec
            elif state == 'findhandler':  # looking for handler name
                handler = line.split()
                if not (len(handler) >= 2 and handler[0] == "void") and \
                   not (len(handler) >= 3 and handler[0] == "static" and \
                        handler[1] == "void"):
                    print("o2_idc: Expected void <handlername>(... here:",
                          line)
                    return False
                # skip [static] void
                handler = handler[1] if handler[0] == "void" else handler[2]
                handler = handler.split("(")  # remove parameter list
                handler = handler[0].strip()  # just to be safe
                if line.find("{") >= 0:
                    state = 'skipargs'
                else:
                    state = 'findbrace'
                outf.write(orig_line);
            elif state == 'findbrace':
                if line.find("{") >= 0:
                    state = 'skipargs'
                outf.write(orig_line);
            elif state == 'skipargs':  # skip the machine-generated arg unpack
                if line.find("// begin unpack message (machine-generated):") \
                   >= 0:
                    state = 'skippingargs'
                    # do not write this line to output file
                else:
                    if not unpack_args(outf, o2id_type, description, 
                                       handler, methods):
                        return False
                    outf.write(orig_line)
                    state = 'skipandfindid'
            elif state == 'skippingargs':
                if line.find("// end unpack message") >= 0:
                    if not unpack_args(outf, o2id_type, description,
                                       handler, methods):
                        return False
                    state = 'skipandfindid'
            elif state == 'skipinit':
                if line.find("END INTERFACE INITIALIZATION") >= 0:
                    write_initialize(outf, o2id_type, methods)
                    wrote_initialize = True
                    state = 'findid'
            else:
                print("o2idc: unexpected condition, state", state)
                return False
    if state == 'findid' and not wrote_initialize:
        if o2id_type:
            filebase = os.path.basename(filename)
            filebase = os.path.splitext(filebase)[0]
            print("void", filebase + "_initialize()\n{", file=outf)
            write_initialize(outf, o2id_type, methods);
            print("}", file=outf)
        else:
            print("o2idc: Unexpected condition. o2id_type is not defined.")
            print("    Maybe no INTERFACE directives found in this file.")
    elif state != 'findid':
        print("o2idc: something was not closed properly. Unexpected state")
        print("    at the end of", filename, " (final state", state, ")")
        return False
    outf.close()
    return True


def process_and_cleanup(filename):
    """generate to filename.output. If error, just report error and
    delete filename.output. If no error, rename the original to
    filename.bak<N> for N in 1, 2, 3, ..., and rename filename.output
    to filename."""
    output_filename = filename + ".output"
    if process(filename, output_filename):
        # search for unused backup name
        in_use = True
        backup_number = 1
        while in_use:
            backupname = filename + ".bak" + str(backup_number)
            if not os.path.exists(backupname):
                in_use = False
            else:
                backup_number += 1
        os.rename(filename, backupname)
        os.rename(output_filename, filename)
        print("        o2idc: rewrote", filename, "and left backup in", \
              backupname)
    else:
        os.unlink(output_filename)
        print("        o2idc: removed", output_filename, \
              "after error encountered.")


def run():
    for filename in sys.argv[1:]:
        process_and_cleanup(filename)

run()
