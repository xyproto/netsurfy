JavaScript bindings
===================

In order for JavaScript programs to to interact with the page contents
it must use the Document Object Model (DOM) and Cascading Style Sheet
Object Model (CSSOM) API.

These interfaces are described using Web Interface Description
Language (WebIDL) within the relevant specifications
(e.g. https://dom.spec.whatwg.org/).

Each interface described by the WebIDL must be bound (connected) to
the browsers internal representation for the DOM or CSS, etc. These
bindings descriptions are processed together with the WebIDL by the
nsgenbind tool to generate source code.

A list of [DOM and CSSOM methods](unimplemented.html) is available
outlining the remaining unimplemented API bindings.

WebIDL
------

The [WebIDL specification](http://www.w3.org/TR/WebIDL/) defines the
interface description language used. The WebIDL is being updated and
an [editors draft](https://heycam.github.io/webidl/) is available but
use is inconsistent.

These descriptions should be periodically updated to keep the browser
interfaces current.

There is a content/handlers/javascript/WebIDL/Makefile which attempts
to automatically update the IDL files by scraping the web specs.

This tool needs a great deal of hand holding, not least because many of the
source documents list the IDL fragments multiple times, some even have
appendices with the entire IDL repeated.

The IDL uses some slightly different terms than other object orientated
 systems.

  WebIDL | JavaScript | Common OOP | Note
 ------- | ---------- | ---------- | ----
 interface | prototype | class     | The data definition of the object
 constants | read-only value property on the prototype | class variable | Belong to class, one copy
 operation | method    | method    | functions that can be called
 attribute | property  | property  | Variables set per instance

JavaScript implementation
-------------------------

NetSurfy uses the system QuickJS-ng library to run the JavaScript
used within the browser. The QuickJS API is declared and documented in
`quickjs.h`, and https://quickjs-ng.github.io/quickjs/ describes the
engine and its embedding API.

Values are passed around as `JSValue`, which is reference counted.
Every value returned by a `JS_` function or a binding must either be
returned to the caller or released with `JS_FreeValue()`.

Qjsky
-----

The layer between QuickJS and the browser is called `qjsky`. It
defines the conventions and capabilities which are common to almost
all bindings. The header `qjsky.h` provides the interface to these
functions.

Normally these functions are used by automatically generated content,
but if a binding needs to return DOM nodes to JavaScript (for example
from a method implementation) `qjsky_node()` should be used, and when
calling a JavaScript function `qjsky_call()` should be used.

Qjsky interrupts any JavaScript execution which lasts longer than the
`script_timeout` option (10 seconds by default). If you are calling a
JavaScript function from outside the normal paths through which qjsky
runs code on your behalf (`js_exec()`, events, timers, etc) then call
`qjsky_call()` with `reset_timeout` set to `true`, otherwise your code
may be terminated early.

Parts of the web platform are implemented in JavaScript, in
`content/handlers/javascript/quickjs/prelude.js`. It runs in every new
context before any page script and provides the selector engine
(`querySelector`, `querySelectorAll`, `matches`, `closest`),
`matchMedia`, custom elements, `fetch` with `Headers`, `Request`,
`Response` and `AbortController`, and `TextEncoder`/`TextDecoder`. It
receives a small object of native helpers from `qjsky.c` (network
requests through the fetch layer, URL resolution, origins and UTF-8
conversion) and returns the generics used by the bindings.

Interface binding introduction
------------------------------

The binding files are processed by the nsgenbind tool to generate c
source code which implements the interfaces within the JavaScript
engine.

The bindings are specific to a JavaScript engine, the DOM library, the
CSS library and the browser. In this case that is the tuple of
QuickJS, libdom, libcss and NetSurfy.

In principle other engines or libraries could be substituted
(historically NetSurfy unsuccessfully tried to use spidermonkey) but the
engineering to do so is formidable.

The bindings are kept the main [NetSurfy source code
repository](https://github.com/xyproto/netsurfy) within the
QuickJS JavaScript handler directory `content/handlers/javascript/quickjs/`

The root binding which contains all the interfaces introduced into
the JavaScript programs initial execution context is netsurfy.bnd, this
references all the WebIDL to be bound and includes all additional
binding definitions to implement the interfaces.

The bindings are a Domain Specific Language (DSL) which allows
implementations to be added to each WebIDL method. nsgenbind
documentation contains a [full description of the
language](https://ci.netsurf-browser.org/jenkins/view/Categorized/job/docs-nsgenbind/doxygen/index.html).

The main focus on creating binding is to implement the content within
getter, setter and method stanzas. These correspond to implementations
of the WebIDL operations on an interface.

The binding implementations are in the form of C code fragments
directly pasted into the generated code with generated setup code
surrounding it.

### Simple attribute example

The Window interface (class) in the HTML specification has an
attribute (property) called `name`.

The full WebIDL for the Window interface is defined in the
`content/handlers/javascript/WebIDL/html.idl` file but the
fragment for our example is just:

    interface Window : EventTarget {
        attribute DOMString name;
    };

This indicates there is an attribute called `name` which is a string
(technicaly a DOMString but the implementation does not differentiate
between string types) which means it has both a setter and a getter.

attributes can be marked readonly which would mean there is only a
getter required for them. For example the Plugin interface has a
'name' attribute defined as:

    interface Plugin {
        readonly attribute DOMString name;
    };

The getter and setter for the Window class attribute will be added to
`Window.bnd`. The entries added will be of the form:

    getter Window::name()
    %{
    %}
    
    setter Window::name()
    %{
    %}

The top level `netsurfy.bnd` binding includes `Window.bnd` (using a
`#include` directive) which contains the implementation of the Window
class. This is purely to split the bindings up into logical units.

The nsgenbind tool generates code that automatically allows acess to
the classes private data structure elements through a variable called
`priv`, the QuickJS context in the variable `ctx`, the object the
call was made on in `this_val` and the arguments in `argc` and `argv`.

The getter binding code returns the retrieved value as a `JSValue`, or
`JS_EXCEPTION` after throwing an error.

So for the name attribute case the complete getter binding is:

    getter Window::name()
    %{
            const char *name = NULL;
            browser_window_get_name(priv->win, &name);
            return JS_NewString(ctx, name != NULL ? name : "");
    %}

This uses the browser_window_get_name() interface to retrieve the name
string for the window (identified using the private context) and then
returns it as a new JavaScript string.

The setter receives the new value in `argv[0]`, updates the internal
private data structure with that value and returns `JS_UNDEFINED`, or
`JS_EXCEPTION` if the value could not be converted.

So for the name attribute case the complete setter binding is:

    setter Window::name()
    %{
            const char *name = JS_ToCString(ctx, argv[0]);
            if (name == NULL) return JS_EXCEPTION;
            browser_window_set_name(priv->win, name);
            JS_FreeCString(ctx, name);
            return JS_UNDEFINED;
    %}

This uses browser_window_set_name() interface to set the name of the
window. The return indicates the success of the operation.

### Simple method example

The Location interface (class) in the HTML specification has an
operation (method) called `asign`. This method causes the browser to
navigate to a new url.

The full WebIDL for the Window interface is defined in the
`content/handlers/javascript/WebIDL/html.idl` file but the
fragment for our example is:

    interface Location {
            void assign(DOMString url);
    }

This shows there is an operation called assign in the Location
interface which takes a single string parameter and returns nothing.

The method binding will be added to `Location.bnd` similarly to how
the attribute example used `Window.bnd`

    method Location::assign()
    %{
            window_private_t *priv_win = qjsky_global_private(ctx);
            const char *url;
            nsurl *joined;

            if (priv_win == NULL || priv_win->win == NULL) {
                    NSLOG(netsurfy, INFO, "failed to get browser context");
                    return JS_UNDEFINED;
            }

            url = JS_ToCString(ctx, argv[0]);
            if (url == NULL) {
                    return JS_EXCEPTION;
            }

            if (nsurl_join(priv->url, url, &joined) == NSERROR_OK) {
                    browser_window_navigate(priv_win->win,
                            joined,
                            NULL,
                            BW_NAVIGATE_HISTORY,
                            NULL,
                            NULL,
                            NULL);
                    nsurl_unref(joined);
            }
            JS_FreeCString(ctx, url);
            return JS_UNDEFINED;
    %}

The nsgenbind tool generates code that automatically allows acess to
the classes private data structure elements through a variable called
`priv`, the QuickJS context in the variable `ctx`, the object the
call was made on in `this_val` and the arguments in `argc` and `argv`.

nsgenbind generates code which makes sure `argv` holds at least as
many values as the operation has parameters, filling missing ones
with `undefined` or their WebIDL default value.

The binding implementation shown here uses browser_window_navigate()
to navigate to the new url. To do this it needs the browser window
handle (pointer) which is obtained from the global object (the window)
private structure with `qjsky_global_private()`.

The string returned by `JS_ToCString()` must be released with
`JS_FreeCString()`.

### Overloaded method example

The Window interface (class) in the HTML specification has an
operation (method) called `alert`. This method is supposed to cause
the user to be alerted about something on the page.

The full WebIDL for the Window interface is defined in the
`content/handlers/javascript/WebIDL/html.idl` file but the
fragment for our example is:

    interface Window : EventTarget {
          void alert();
          void alert(DOMString message);
    };

This indicates there is an operation called `alert`. It has two
overloaded prototypes, one takes no parameters and one takes a string
parameter (technicaly a DOMString but the implementation does not
differentiate between string types)

The method binding will be added to `Window.bnd` as the attribute example

    method Window::alert()
    %{
            if (argc == 0) {
                    NSLOG(netsurfy, INFO, "JS ALERT");
            } else {
                    size_t msg_len;
                    const char *msg = JS_ToCStringLen(ctx, &msg_len, argv[0]);
                    if (msg == NULL) return JS_EXCEPTION;
                    NSLOG(netsurfy, INFO, "JS ALERT: %*s", (int)msg_len, msg);
                    JS_FreeCString(ctx, msg);
            }
            return JS_UNDEFINED;
    %}

The nsgenbind tool generates code that automatically allows acess to
the classes private data structure elements through a variable called
`priv`, the QuickJS context in the variable `ctx`, the object the
call was made on in `this_val` and the arguments in `argc` and `argv`.

For overloaded method calls nsgenbind does not emit code to do
parameter verification and the binding code has to deal with all
possible parameters itself.

This binding checks the number of parameters and if one is present it
coerces it to be a string and logs the result.