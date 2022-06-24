# LSP server for [Jakt](https://github.com/SerenityOS/jakt)

This is a middleware that talks both to the LSP client and to the Jakt compiler. It receives
requests from LSP client in one format, queries information about the code to Jakt compiler,
and answers back to the client that requested it.

Target Features:

- [ ] Basic RPC support
- [ ] Goto Definition
- [ ] Find all references
- [ ] Hover
- [ ] Diagnostics
- [ ] Rename
- [ ] Auto completion
- [ ] Intellisense
- [ ] Signature help
- [ ] Document symbols

# Architecture

To support cancellation of requests, the server will use a task & worker model:
We have some workers that communicate with main thread via mpsc/spsc channels. The main
thread keeps track of what tasks are in progress, to be done or have a result/error, and have its
own queue to package those responses into JSON objects and send them back to the client.

Tasks will only get created for requests that are parsed correctly (i.e correct method and parameters).
