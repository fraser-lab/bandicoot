#   remote_control.py
#   Copyright (C) 2008, 2009  Bernhard Lohkamp, University of York
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   Bandicoot v0.1.0.0: Py2 -> Py3 conversion. The original was full of
#   `print "x"` statements and Py2-style exec. Socket send/recv now
#   bytes-aware. The rest of the file's logic is unchanged.

global coot_listener_socket
coot_listener_socket = False

def open_coot_listener_socket(port_number, host_adress="127.0.0.1"):
    import socket
    global coot_listener_socket

    print("in open_coot_listener_socket port: %s host %s" % (port_number, host_adress))

    soc = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    soc.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Original Coot code does soc.connect() here, which makes the
    # process a CLIENT to Phenix's server. That matches Phenix's
    # design: Phenix listens, Coot connects in. Bind/listen reversal
    # would be wrong.
    try:
        soc.connect((host_adress, port_number))
    except Exception as e:
        print("BL INFO:: cannot connect to socket on host %s with port %s: %s" %
              (host_adress, port_number, e))
        return

    print("Coot listener socket ready!")
    soc.send(b"Coot listener socket ready!\n")
    coot_listener_socket = soc
    if soc:
        set_coot_listener_socket_state_internal(1)

    # The original threads through run_python_thread; if that helper
    # isn't yet ported to Py3 the call will raise. Caller (C side)
    # doesn't check the return, so an error here just leaves the
    # listener inactive but doesn't bring down Coot.
    try:
        status = run_python_thread(coot_listener_idle_function_proc, ())
    except NameError:
        print("BL WARNING:: run_python_thread not defined; socket established but idle loop not running")


def open_coot_listener_socket_with_timeout(port_number, host_adress="127.0.0.1"):
    try:
        from gi.repository import GObject as gobject
    except Exception:
        print("BL WARNING:: no gobject available, so no socket. Sorry!")
        return

    import socket
    global coot_listener_socket

    print("in open_coot_listener_socket port: %s host %s" % (port_number, host_adress))

    soc = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        soc.connect((host_adress, port_number))
    except Exception:
        print("BL INFO:: cannot connect to socket on host %s with port %s" %
              (host_adress, port_number))
        return

    print("Coot listener socket ready!")
    soc.send(b"Coot listener socket ready!\n")
    coot_listener_socket = soc

    gobject.timeout_add(1000, coot_socket_timeout_func)


def coot_socket_timeout_func():
    global coot_listener_socket
    global total_socket_data
    if coot_listener_socket:
        while True:
            continue_qm = listen_coot_listener_socket(coot_listener_socket)
            if not continue_qm:
                set_coot_listener_socket_state_internal(0)
                try:
                    coot_listener_socket.shutdown(2)
                except Exception:
                    try:
                        coot_listener_socket.shutdown(1)
                    except Exception:
                        print("BL INFO:: problems shutting down the controling socket, ")
                        print("maybe already down")
                coot_listener_socket.close()
                coot_listener_socket = False
                print("server gone - listener thread ends", coot_listener_socket)
                return False
            else:
                return True
    else:
        print("coot_socket_timeout_func bad sock: ", coot_listener_socket)
        return False


def coot_listener_error_handler(key, args=""):
    print("coot_listener_error_handler handling error in %s with args %s" % (key, args))


def coot_listener_idle_function_proc():
    global coot_listener_socket
    import time
    if coot_listener_socket:
        while True:
            continue_qm = listen_coot_listener_socket(coot_listener_socket)
            if not continue_qm:
                set_coot_listener_socket_state_internal(0)
                try:
                    coot_listener_socket.shutdown(2)
                except Exception:
                    try:
                        coot_listener_socket.shutdown(1)
                    except Exception:
                        print("BL INFO:: problem shutting down the controling socket, ")
                        print("maybe already down")
                coot_listener_socket.close()
                coot_listener_socket = False
                print("server gone - listener thread ends", coot_listener_socket)
                return
            else:
                time.sleep(0.01)
                return
    else:
        print("coot_listener_idle_func_proc bad sock: ", coot_listener_socket)


global total_socket_data
total_socket_data = []


def listen_coot_listener_socket(soc):
    global total_socket_data
    close_connection_string = "# close"
    end_connection_string = "# end"

    def evaluate_character_list(string_data):
        print("received in evaluate", string_data)
        for line in string_data.split("\n"):
            if line == close_connection_string:
                print("finish socket")
                return False
            try:
                ret = eval(line)
            except SyntaxError:
                if line == "\n":
                    print("CR")
                try:
                    exec(line, globals())
                    ret = None
                except Exception:
                    ret = "INFO:: cannot eval or exec given string: " + line
            except Exception:
                ret = "Info:: input error"
            ret = "return value: " + str(ret)
            try:
                soc.send(ret.encode() if isinstance(ret, str) else ret)
            except Exception:
                pass
        return True

    data = False
    soc.setblocking(0)

    def check_aliveness():
        try:
            soc.sendall(b"")
        except Exception:
            print("BL INFO:: appear that serve is down, closing down connection")
            return False
        return True

    try:
        while True:
            data = soc.recv(1024)
            if isinstance(data, bytes):
                data = data.decode("utf-8", errors="replace")

            import time
            time.sleep(0.1)
            if end_connection_string in data:
                total_socket_data.append(data[:data.rfind(end_connection_string)])
                break

            if data != "":
                total_socket_data.append(data)
            else:
                return check_aliveness()

            if len(total_socket_data) > 1:
                last_pair = total_socket_data[-2] + total_socket_data[-1]
                if end_connection_string in last_pair:
                    total_socket_data[-2] = last_pair[:last_pair.find(end_connection_string)]
                    total_socket_data.pop()
                    break
        total_socket_data = ''.join(total_socket_data)
        if not data:
            return True
        else:
            ret = evaluate_character_list(total_socket_data)
            total_socket_data = []
            return ret
    except Exception:
        return True
