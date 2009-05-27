#!/usr/bin/env python

import iris
import gtk
import gobject

print dir(iris)
print
print

print dir(iris.Task)
print
print

print iris.Scheduler.default()
print
print

t = iris.Task()
print iris.Task.all_of(t)
print
print

t = iris.Task()
print iris.Task.any_of(t)
print
print

iris.Scheduler.print_stat()
print

def handler(msg, data = None):
    print "Message:", msg, "Data:", data
    gtk.main_quit()

p = iris.Port()
#r = iris.Arbiter.receive(p, lambda _: None, data = None, scheduler = None)
#r = iris.Arbiter.receive(port = p, handler = lambda _: None)
r = iris.Arbiter.receive(p, handler, data=('my', 'user', 'data'))
c = iris.Arbiter.coordinate(r)

print 'Port:        ', p
print 'Receiver:    ', r
print 'Coordinator: ', c
print

gobject.timeout_add(0, lambda: p.post(1,2,3,4))
gtk.main()
