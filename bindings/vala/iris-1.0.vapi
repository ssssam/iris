/* iris-1.0.vapi
 *
 * Copyright (C) 2009 Christian Hergert <chris@dronelabs.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 
 * 02110-1301 USA
 */

using GLib;

[CCode (cheader_filename="iris/iris.h")]
namespace Iris {
	namespace SchedulerManager {
		public void prepare (Iris.Scheduler scheduler);
		public void unprepare (Iris.Scheduler scheduler);
		public void request (Iris.Scheduler scheduler, uint per_quantum, uint total);
		public void print_stat ();
	}
	public class Scheduler: GLib.Object {
		public Scheduler ();
		public Scheduler.full (uint min_threads, uint max_threads);
		public static Scheduler default ();
		public virtual int get_max_threads ();
		public virtual int get_min_threads ();
	}
	public class WSScheduler: Iris.Scheduler {
		public WSScheduler ();
		public WSScheduler.full (int min_threads, int max_threads);
	}
	public class LFScheduler: Iris.Scheduler {
		public LFScheduler ();
		public LFScheduler.full (int min_threads, int max_threads);
	}
	public delegate void TaskFunc (Task task);
	public class Task: GLib.Object {
		public static Task any_of (Iris.Task first_task, ...);
		public static Task all_of (Iris.Task first_task, ...);
		[CCode (instance_pos = "-1")]
		public Task (Iris.TaskFunc? func, GLib.DestroyNotify? notify = null);
		public Task.from_closure (GLib.Closure closure);
		[CCode (instance_pos = "1")]
		public Task.full (Iris.TaskFunc? func, GLib.DestroyNotify? notify = null, bool async = false, Iris.Scheduler? scheduler = null, GLib.MainContext? context = null);
		public void run ();
		public Iris.Scheduler get_scheduler ();
		public void set_scheduler (Iris.Scheduler scheduler);
		public GLib.MainContext? get_main_context ();
		public void set_main_context (GLib.MainContext? context);
		public bool is_async ();
		public bool is_executing ();
		public bool is_canceled ();
		public bool is_finished ();
		public void add_dependency (Task task);
		public void remove_dependency (Task task);
		public void cancel ();
		public void complete ();
		[CCode (instance_pos = "-1")]
		public void add_callback (Iris.TaskFunc? func, GLib.DestroyNotify? notify = null);
		[CCode (instance_pos = "-1")]
		public void add_errback (Iris.TaskFunc? func, GLib.DestroyNotify? notify = null);
		public void add_callback_closure (GLib.Closure closure);
		public void add_errback_closure (GLib.Closure closure);
		public weak GLib.Error? get_error ();
		public void set_error (GLib.Error? error);
		public void take_error (GLib.Error error);
		public Value get_result ();
		public void set_result (Value value);
		public void set_result_gtype (GLib.Type type, ...);
	}
	[CCode (ref_function = "iris_queue_ref", unref_function = "iris_queue_unref")]
	public class Queue: GLib.Boxed {
		public Queue ();
		public void push (void* data);
		public void* pop ();
		public void* try_pop ();
		public void* timed_pop (GLib.TimeVal timeout);
		public uint length ();
	}
	public class LFQueue: Iris.Queue {
	}
	public class WSQueue: Iris.Queue {
		public WSQueue (Iris.Queue global, Iris.RRobin rrobin);
		public void* try_steal (uint timeout = 0);
		public void local_push (void* data);
		public void* local_pop ();
	}
	[CCode (instance_pos = "-1")]
	public delegate void RRobinFunc (void *data);
	[CCode (instance_pos = "-1")]
	public delegate bool RRobinForeachFunc (Iris.RRobin rrobin, void *data);
	[CCode (ref_function = "iris_rrobin_ref", unref_function = "iris_rrobin_unref")]
	public class RRobin: Boxed {
		public RRobin (int size);
		public bool append (void* data);
		public void remove (void* data);
		[CCode (instance_pos = "-1")]
		public bool apply (Iris.RRobinFunc func);
		[CCode (instance_pos = "-1")]
		public void foreach (Iris.RRobinForeachFunc func);
	}
	[CCode (instance_pos = "-1")]
	public delegate void MessageHandler (Iris.Message message);
	[CCode (ref_function = "iris_message_ref", unref_function = "iris_message_unref")]
	public class Message: Boxed {
		public int what;
		public Message (int what);
		public Message.data (int what, GLib.Type type, ...);
		public Message.full (int what, string first_name, ...);
		public Message copy ();
		public weak Value get_data ();
		public void set_data (Value value);
		public uint count_names ();
		public bool is_empty ();
		public void get_value (string name, ref GLib.Value value);
		public void set_value (string name, GLib.Value value);
		public string get_string (string name);
		public void set_string (string name, string value);
		public int get_int (string name);
		public void set_int (string name, int value);
		public int64 get_int64 (string name);
		public void set_int64 (string name, int64 value);
		public float get_float (string name);
		public void set_float (string name, float value);
		public double get_double (string name);
		public void set_double (string name, double value);
		public long get_long (string name);
		public void set_long (string name, long value);
		public ulong get_ulong (string name);
		public void set_ulong (string name, ulong value);
		public char get_char (string name);
		public void set_char (string name, char value);
		public uchar get_uchar (string name);
		public void set_uchar (string name, uchar value);
		public bool get_boolean (string name);
		public void set_boolean (string name, bool value);
		public size_t flattened_size ();
		public bool flatten (ref void* buffer, ref size_t length);
		public bool unflatten (void* buffer, size_t length);
	}
	public class Thread {
		public static Thread? get ();
		public void manage (Iris.Queue queue, bool leader);
		public void shutdown ();
		public void print_stat ();
	}
	public class Port: GLib.Object {
		public Port ();
		public virtual void post (Iris.Message message);
		public virtual void repost (Iris.Message message);
		public virtual Iris.Receiver? get_receiver ();
		public virtual void set_receiver (Iris.Receiver? receiver);
		public void flush ();
		public bool has_receiver ();
		public uint get_queue_count ();
	}
	public class Arbiter: GLib.Object {
		public virtual void can_receive (Iris.Receiver receiver);
		public virtual void receive_completed (Iris.Receiver receiver);
		public static Arbiter coordinate (Iris.Receiver? exclusive, Iris.Receiver? concurrent, Iris.Receiver? teardown);
		[CCode (instance_pos = "-1")]
		public static Receiver receive (Iris.Scheduler? scheduler, Iris.Port port, Iris.MessageHandler handler);
	}
	public class Receiver: GLib.Object {
		public void set_scheduler (Iris.Scheduler scheduler);
		public Iris.Scheduler get_scheduler ();
		public void deliver (Iris.Message message);
	}
}
