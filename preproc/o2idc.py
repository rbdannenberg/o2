# o2idc.py -- Interface Description Compiler for O2 messages
#
# Roger B. Dannenberg
# Dec 2021

# Syntax: Service methods are described in a group within a
# /* ... */-style comment. Everything in the file is ignored
# before "'O2 INTERFACE DESCRIPTION FOR SERVICE: <service> <newline>",
# where <service> is the service name. 
#
# Then, each method of the service is described as follows:
# <name>(<type> <param>, <type> <param>, ...) -- comment <newline><newline>
# where the full O2 address will be "!<service>/<name>" and the 
# parameters will have the <type>'s declared within parentheses.
# For O2lite compatibility, types are int32, int64, float, double,
# and string. The <param>'s are C identifiers, but their only
# purpose is to make the method more understandable. The description
# after the close parenthesis ")" is ignored up to the next blank line.
#
# Any number of methods may be declared, separated by blank lines.
#
# The declarations are terminated a line containing "*/". The entire
# line is ignored, so put "*/" on a line by itself.


import sys
import os


# def found_method(desc, filename):
#     """returns false if an error occurs
#     """
#     print("process found method", desc, "in", filename)
#     pos = desc.find("(")
#     if pos < 1:
#         print("o2idc: expected method name in file", filename, "description",
#               desc)
#         return False
#     method = desc[0:pos].strip()
#     params = []
#     while pos < len(desc) and desc[pos] != ")":
#         pos += 1  # skip over comma or open paren
#         # get a type
#         while pos < len(desc) and desc[pos].isspace():  # skip whitespace
#             pos += 1
#         pos2 = desc.find(" ", pos)
#         if pos2 <= 0:
#             print("o2idc: expected type in file", filename, "after", 
#                   desc)
#             return False
#         type = desc[pos:pos2]
#         
#         # get parameter name
#         while pos2 < len(desc) and desc[pos2].isspace():  # skip whitespace
#             pos2 += 1
#         pos = pos2
#         while pos2 < len(desc) and desc[pos2] not in " ,;":
#             pos2 += 1
#         if pos == pos2:
#             print("o2idc: expected parameter name in file", filename, "after", 
#                   desc[0:pos2])
#             return False
#         param = desc[pos:pos2]
#         params.append((type, param))
#         print("Params: ", (type, param))
# 
#         # get comma or semicolon
#         pos = pos2
#         while pos < len(desc) and desc[pos] not in ",;":
#             print("Scanning at", repr(desc[pos]))
#             if not desc[pos].isspace():
#                 print('o2idc: expected "," or ";" after parameter in file',
#                       filename, "after", desc[0:pos2])
#                 return False
#             pos += 1
# 
#     new_method(method, params)
#     return True


typecodes = {"string": "s", "int32": "i", "int64": "h", "float": "f",
             "double": "d"}
 
 
# typestring = make_typestring(meth[1]);
# 
# def make_typestring(params):
#     # construct o2 typestring from a list of parameter info of the
#     # form [[type, paramname], [type, paramname], ...]
#     ts = ""
#     for p in params:
#         ts += typecodes[p[0]]
#     return ts
# 

# def write_interface_decls(basename, prefix, hfile):
#     global o2id_methods, o2id_service
#     print("void", basename + "_initialize();\n", file=hfile)
#     for meth in o2id_methods:
#         print("void ", o2id_service, "_", meth[0], "_", 
#               "handler(", prefix, "_HANDLER_ARGS);", sep='', file=hfile)




def write_initialize(outf, o2id_type, methods):
    """write o2[l]_method_new calls for methods, which is a list 
    of (address, function, typestring) tuples
    """
    print("    //", o2id_type.upper(), "INTERFACE INITIALIZATION:",
          "(machine generated)", file=outf)
    for meth in methods:
        if o2id_type == 'o2lite':
            print('    o2l_method_new("', meth[0], '", "', meth[2], 
                  '", true, ', meth[1], ', NULL);', sep='', file=outf)
        else:
            print("    ", o2id_type, '_method_new("', meth[0], '", "',
                  meth[2], '", ', meth[1], ', NULL, true, true);', 
                  sep='', file=outf)
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
            print("o2idc: could not parse parameters near", fields)
            print("    expected <type> <name>")
            return False
        typ = fields[0]
        pname = fields[1]
        print("    ", file=outf, end='')
        ctype = "char *" if typ == "string" else (typ + " ")
        typestring = typestring + typecodes[typ]
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


def process(filename):
    print("   ", filename)
    wrote_initialize = False
    methods = []  # list of (address, handler, typestring) pairs
    backupname = filename + ".bak"
    os.rename(filename, backupname)
    outf = open(filename, "w")
    with open(backupname, "r") as file:
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
                        os.rename(backupname, filename)
                        return False
                    outf.write(orig_line)
                    state = 'skipandfindid'
            elif state == 'skippingargs':
                if line.find("// end unpack message") >= 0:
                    if not unpack_args(outf, o2id_type, description,
                                       handler, methods):
                        os.rename(backupname, filename)
                        return False
                    state = 'skipandfindid'
            elif state == 'skipinit':
                if line.find("END INTERFACE INITIALIZATION") >= 0:
                    write_initialize(outf, o2id_type, methods)
                    wrote_initialize = True
                    state = 'findid'
            else:
                print("o2idc: unexpected condition, state", state)
                os.rename(backupname, filename)
                return False
    if state == 'findid' and not wrote_initialize:
        filebase = os.path.basename(filename)
        filebase = os.path.splitext(filebase)[0]
        print("void", filebase + "_initialize()\n{", file=outf)
        write_initialize(outf, o2id_type, methods);
        print("}", file=outf)
    elif state != 'findid':
        print("o2idc: something was not closed properly. Unexpected state")
        print("    at the end of", filename, " (final state", state, ")")
        os.rename(backupname, filename)
        return False
    outf.close()
    return True


def run():
    for filename in sys.argv[1:]:
        process(filename)

run()
