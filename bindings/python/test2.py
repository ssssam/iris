#!/usr/bin/env python

import gtk
import gobject
from   iris import Port, Scheduler, WSScheduler, Arbiter

def onExclusiveMsg(msg, data=None):
    """Handle an exclusive message, such as a state change."""
    print 'exclusive'
    key, value = msg
    data [key] = value

def onConcurrentMsg(msg, data=None):
    """Handle a concurrent message such as a request."""
    if msg[0] == 99:
        print 'Concurrent done'
        gtk.main_quit()

def onTeardownMsg(msg, data=None):
    """Handle a teardown message by stopping the main loop."""
    print 'teardown'
    gtk.main_quit()

def queueMsgs((e, c, t)):
    e.post('count', 10)
    for i in range(100): c.post(i)
    e.post('count', 20)
    #t.post(1)
    return False

def printStat((e,c,t,re,rc,rt,a)):
    Scheduler.print_stat()
    print 'Exclusive: ', e.is_paused() and 'PAUSED' or 'UNPAUSED', e.get_queue_count()
    print 'Concurrent:', c.is_paused() and 'PAUSED' or 'UNPAUSED', c.get_queue_count()
    print 'Teardown:  ', t.is_paused() and 'PAUSED' or 'UNPAUSED', t.get_queue_count()
    #print a.can_receive(re)
    #print a.can_receive(rc)
    #print a.can_receive(rt)
    return True

# use work stealing
sched = WSScheduler()

# our application state
state  = {}

# our exclusive port, only one of these will be processed
# at a time and no teardown or concurrent will be processed
# concurrently
port_e = Port()

# our concurrent port, as many of these will be processed as
# allowed concurrently
port_c = Port()

# our teardown port, after a message is received from this port,
# no more messages will be received, ever.
port_t = Port()

# setup our coordinator to manage the receivers
recv_e = Arbiter.receive(port_e, onExclusiveMsg,  state, scheduler=None)
recv_c = Arbiter.receive(port_c, onConcurrentMsg, state, scheduler=None)
recv_t = Arbiter.receive(port_t, onTeardownMsg,   state, scheduler=None)
a = Arbiter.coordinate(recv_e, recv_c, recv_t)

gobject.timeout_add(0, queueMsgs, (port_e, port_c, port_t))
gobject.timeout_add_seconds(2, printStat, (port_e, port_c, port_t, recv_e, recv_c, recv_t, a))
gtk.main()
