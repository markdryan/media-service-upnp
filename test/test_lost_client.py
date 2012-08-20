import sys
import gobject
import dbus
import dbus.mainloop.glib

def handle_browse_reply(objects):
    print "Total Items: " + str(len(objects))
    print
    loop.quit()

def handle_error(e):
    print "An error occured"
    loop.quit()

def make_async_call():
    root.ListChildrenEx(0,
                        5,
                        ["DisplayName", "Type"],
                        "-DisplayName",
                        reply_handler=handle_browse_reply,
                        error_handler=handle_error)
    root.ListChildrenEx(0,
                        5,
                        ["DisplayName", "Type"],
                        "-DisplayName",
                        reply_handler=handle_browse_reply,
                        error_handler=handle_error)
    root.ListChildrenEx(0,
                        5,
                        ["DisplayName", "Type"],
                        "-DisplayName",
                        reply_handler=handle_browse_reply,
                        error_handler=handle_error)
    root.ListChildrenEx(0,
                        5,
                        ["DisplayName", "Type"],
                        "-DisplayName",
                        reply_handler=handle_browse_reply,
                        error_handler=handle_error)
    root.ListChildrenEx(0,
                        5,
                        ["DisplayName", "Type"],
                        "-DisplayName",
                        reply_handler=handle_browse_reply,
                        error_handler=handle_error)
    # Test: force quit - this should cancel the search on server side
    loop.quit()

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

bus = dbus.SessionBus()
root = dbus.Interface(bus.get_object(
                'com.intel.media-service-upnp',
                '/com/intel/MediaServiceUPnP/server/0'),
            'org.gnome.UPnP.MediaContainer2')

gobject.timeout_add(1000, make_async_call)

loop = gobject.MainLoop()
loop.run()
