/*
 * Copyright/Licensing information.
 */

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "alarm.h"

G_DEFINE_TYPE (Alarm, alarm, G_TYPE_OBJECT);

/* Prototypes and constants for property manipulation */
static void alarm_set_property(GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec);

static void alarm_get_property(GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec);

static void alarm_constructed (GObject *object);

static void alarm_dispose (GObject *object);

static void alarm_gconf_associate_schemas (Alarm *alarm);

static void alarm_gconf_connect (Alarm *alarm);

static void alarm_gconf_disconnect (Alarm *alarm);

static void alarm_gconf_dir_changed (GConfClient *client,
									 guint cnxn_id,
									 GConfEntry *entry,
									 gpointer data);

static void alarm_timer_start (Alarm *alarm);
static void alarm_timer_remove (Alarm *alarm);
static gboolean alarm_timer_is_started (Alarm *alarm);

static void alarm_player_start (Alarm *alarm);
static void alarm_player_stop (Alarm *alarm);
static void alarm_command_run (Alarm *alarm);

#define ALARM_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_ALARM, AlarmPrivate))

typedef struct _AlarmPrivate AlarmPrivate;

struct _AlarmPrivate
{
	GConfClient *gconf_client;
	guint gconf_listener;
	guint timer_id;
	MediaPlayer *player;
};


static GConfEnumStringPair alarm_type_enum_map [] = {
	{ ALARM_TYPE_CLOCK,		"clock" },
	{ ALARM_TYPE_TIMER,		"timer" },
	{ 0, NULL }
};

static GConfEnumStringPair alarm_repeat_enum_map [] = {
	{ ALARM_REPEAT_SUN,	"sun" },
	{ ALARM_REPEAT_MON,	"mon" },
	{ ALARM_REPEAT_TUE,	"tue" },
	{ ALARM_REPEAT_WED,	"wed" },
	{ ALARM_REPEAT_THU,	"thu" },
	{ ALARM_REPEAT_FRI,	"fri" },
	{ ALARM_REPEAT_SAT,	"sat" },
	{ 0, NULL }
};

static GConfEnumStringPair alarm_notify_type_enum_map [] = {
	{ ALARM_NOTIFY_SOUND,	"sound"  },
	{ ALARM_NOTIFY_COMMAND,	"command" },
	{ 0, NULL }
};

/* Property indexes */
static enum {
	PROP_ALARM_0,
	PROP_DIR,
	PROP_ID,
	PROP_TYPE,
	PROP_TIME,
	PROP_ACTIVE,
	PROP_MESSAGE,
	PROP_TIMER,
	PROP_REPEAT,
	PROP_NOTIFY_TYPE,
	PROP_SOUND_FILE,
	PROP_SOUND_LOOP,
	PROP_COMMAND,
	PROP_NOTIFY_BUBBLE
} AlarmProp;

#define PROP_NAME_DIR			"gconf-dir"
#define PROP_NAME_ID			"id"
#define PROP_NAME_TYPE			"type"
#define PROP_NAME_TIME			"time"
#define PROP_NAME_ACTIVE		"active"
#define PROP_NAME_MESSAGE		"message"
#define PROP_NAME_TIMER			"timer"
#define PROP_NAME_REPEAT		"repeat"
#define PROP_NAME_NOTIFY_TYPE	"notify_type"
#define PROP_NAME_SOUND_FILE	"sound_file"
#define PROP_NAME_SOUND_LOOP	"sound_repeat"
#define PROP_NAME_COMMAND		"command"
#define PROP_NAME_NOTIFY_BUBBLE	"notify_bubble"

/* Signal indexes */
static enum
{
  SIGNAL_ALARM,
  SIGNAL_ERROR,
  SIGNAL_PLAYER,
  LAST_SIGNAL
} AlarmSignal;

/* Signal identifier map */
static guint alarm_signal[LAST_SIGNAL] = {0, 0, 0};

/* Prototypes for signal handlers */
static void alarm_alarm (Alarm *alarm);
static void alarm_error (Alarm *alarm, GError *err);
static void alarm_player_changed (Alarm *alarm, MediaPlayerState state);

/* For debugging purposes only */
static void
dump_list (GSList *list)
{
	GSList *l;
	
	g_print ("[ ");
	for (l = list; l; l = l->next) {
		g_print ("%s", (gchar *)l->data);
		if (l->next)
			g_print (", ");
	}
	g_print (" ]");
}


/* Initialize the Alarm class */
static void 
alarm_class_init (AlarmClass *class)
{
	GParamSpec *dir_param;
	GParamSpec *id_param;
	GParamSpec *type_param;
	GParamSpec *time_param;
	GParamSpec *active_param;
	GParamSpec *message_param;
	GParamSpec *timer_param;
	GParamSpec *repeat_param;
	GParamSpec *notify_type_param;
	GParamSpec *sound_file_param;
	GParamSpec *sound_loop_param;
	GParamSpec *command_param;
	GParamSpec *notify_bubble_param;
	
	GObjectClass *g_object_class;

	/* get handle to base object */
	g_object_class = G_OBJECT_CLASS (class);

	/* << miscellaneous initialization >> */

	/* create GParamSpec descriptions for properties */
	dir_param = g_param_spec_string (PROP_NAME_DIR,
									 "GConf dir",
									 "GConf base directory",
									 NULL,
									 G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
	
	id_param = g_param_spec_uint (PROP_NAME_ID,
								  "alarm id",
								  "id of the alarm",
								  0,		/* min */
								  UINT_MAX,	/* max */
								  0,		/* default */
								  G_PARAM_CONSTRUCT | G_PARAM_READWRITE);
	
	type_param = g_param_spec_uint (PROP_NAME_TYPE,
									"alarm type",
									"type of the alarm",
									ALARM_TYPE_CLOCK,
									ALARM_TYPE_TIMER,
									ALARM_DEFAULT_TYPE,
									G_PARAM_READWRITE);
	
	
	time_param = g_param_spec_uint (PROP_NAME_TIME,
									 "alarm time",
									 "time when the alarm should trigger",
									 0,					/* min */
									 UINT_MAX,			/* max */
									 0,					/* default */
									 G_PARAM_READWRITE);
	
	active_param = g_param_spec_boolean (PROP_NAME_ACTIVE, 
										 "active alarm",
										 "whether the alarm is on or not",
										 ALARM_DEFAULT_ACTIVE,
										 G_PARAM_READWRITE);

	message_param = g_param_spec_string (PROP_NAME_MESSAGE,
										 "alarm message",
										 "message which describes this alarm",
										 ALARM_DEFAULT_MESSAGE,
										 G_PARAM_READWRITE);
	
	timer_param = g_param_spec_uint (PROP_NAME_TIMER,
									 "alarm timer",
									 "storage for the timer alarm type",
									 0,					/* min */
									 UINT_MAX,			/* max */
									 0,					/* default */
									 G_PARAM_READWRITE);
	
	repeat_param = g_param_spec_uint (PROP_NAME_REPEAT,
									  "repeat",
									  "repeat the alarm",
									  ALARM_REPEAT_NONE,	/* min */
									  ALARM_REPEAT_ALL,		/* max */
									  ALARM_DEFAULT_REPEAT,	/* default */
									  G_PARAM_READWRITE);
	
	notify_type_param = g_param_spec_uint (PROP_NAME_NOTIFY_TYPE,
										"notification type",
										"what kind of notification should be used",
										ALARM_NOTIFY_SOUND,
										ALARM_NOTIFY_COMMAND,
										ALARM_DEFAULT_NOTIFY_TYPE,
										G_PARAM_READWRITE);
	
	sound_file_param = g_param_spec_string (PROP_NAME_SOUND_FILE,
										 "sound file",
										 "sound file to play",
										 ALARM_DEFAULT_SOUND_FILE,
										 G_PARAM_READWRITE);
	
	sound_loop_param = g_param_spec_boolean (PROP_NAME_SOUND_LOOP,
											 "sound loop",
											 "whether the sound should be looped",
											 ALARM_DEFAULT_SOUND_LOOP,
											 G_PARAM_READWRITE);
	
	command_param = g_param_spec_string (PROP_NAME_COMMAND,
										 "command",
										 "command to run",
										 ALARM_DEFAULT_COMMAND,
										 G_PARAM_READWRITE);
	
	notify_bubble_param = g_param_spec_boolean (PROP_NAME_NOTIFY_BUBBLE, 
												"notification bubble",
												"whether the alarm should display a notification bubble when triggered",
												ALARM_DEFAULT_NOTIFY_BUBBLE,
												G_PARAM_READWRITE);
	
	
	/* override base object methods */
	g_object_class->set_property = alarm_set_property;
	g_object_class->get_property = alarm_get_property;
	g_object_class->constructed	 = alarm_constructed;
	g_object_class->dispose		 = alarm_dispose;

	/* install properties */
	g_object_class_install_property (g_object_class, PROP_DIR, dir_param);
	g_object_class_install_property (g_object_class, PROP_ID, id_param);
	g_object_class_install_property (g_object_class, PROP_TYPE, type_param);
	g_object_class_install_property (g_object_class, PROP_TIME, time_param);
	g_object_class_install_property (g_object_class, PROP_ACTIVE, active_param);
	g_object_class_install_property (g_object_class, PROP_MESSAGE, message_param);
	g_object_class_install_property (g_object_class, PROP_TIMER, timer_param);
	g_object_class_install_property (g_object_class, PROP_REPEAT, repeat_param);
	g_object_class_install_property (g_object_class, PROP_NOTIFY_TYPE, notify_type_param);
	g_object_class_install_property (g_object_class, PROP_SOUND_FILE, sound_file_param);
	g_object_class_install_property (g_object_class, PROP_SOUND_LOOP, sound_loop_param);
	g_object_class_install_property (g_object_class, PROP_COMMAND, command_param);
	g_object_class_install_property (g_object_class, PROP_NOTIFY_BUBBLE, notify_bubble_param);
	
	g_type_class_add_private (class, sizeof (AlarmPrivate));
	
	/* set signal handlers */
	class->alarm = alarm_alarm;
	class->error = alarm_error;
	class->player_changed = alarm_player_changed;

	/* install signals and default handlers */
	alarm_signal[SIGNAL_ALARM] = g_signal_new ("alarm",		/* name */
											   TYPE_ALARM,	/* class type identifier */
											   G_SIGNAL_RUN_LAST, /* options */
											   G_STRUCT_OFFSET (AlarmClass, alarm), /* handler offset */
											   NULL, /* accumulator function */
											   NULL, /* accumulator data */
											   g_cclosure_marshal_VOID__VOID, /* marshaller */
											   G_TYPE_NONE, /* type of return value */
											   0);
	
	alarm_signal[SIGNAL_ERROR] = g_signal_new ("error",
											   TYPE_ALARM,
											   G_SIGNAL_RUN_LAST,
											   G_STRUCT_OFFSET (AlarmClass, error),
											   NULL,
											   NULL,
											   g_cclosure_marshal_VOID__POINTER,
											   G_TYPE_NONE,
											   1,
											   G_TYPE_POINTER);
	
	alarm_signal[SIGNAL_PLAYER] = g_signal_new ("player_changed",		/* name */
												TYPE_ALARM,	/* class type identifier */
												G_SIGNAL_RUN_LAST, /* options */
												G_STRUCT_OFFSET (AlarmClass, player_changed), /* handler offset */
												NULL, /* accumulator function */
												NULL, /* accumulator data */
												g_cclosure_marshal_VOID__UINT, /* marshaller */
												G_TYPE_NONE, /* type of return value */
												1,
												G_TYPE_UINT);
}

/*
 * Utility function for extracting the strings out of a GConfValue of type
 * GCONF_VALUE_LIST and placing them in a plan GSList of strings.
 * 
 * Note: You should free the GSList returned but NOT the string contents.
 */
static GSList *
alarm_gconf_extract_list_string (GConfValue *val)
{
	GSList *list, *new_list, *l;
	
	g_assert (GCONF_VALUE_STRING == gconf_value_get_list_type (val));
	
	/* Fetch GSList of GConfValues. Extract them and put into a plain list of strings.
	 * Note that the returned string from gconf_value_get_string() is owned by the GConfValue
	 * and should thus NOT be freed.
	 */
	list = gconf_value_get_list (val);
	new_list = NULL;
	for (l = list; l; l = l->next) {
		new_list = g_slist_append (new_list, (gpointer) gconf_value_get_string ((GConfValue *)l->data));
	}
	
	return new_list;
}

static void
alarm_gconf_load (Alarm *alarm)
{
	AlarmPrivate *priv	= ALARM_PRIVATE (alarm);
	GConfClient *client = priv->gconf_client;
	GConfValue *val;
	gchar *key, *tmp;
	GSList *list;
	guint i;
	
	/*
	 * TYPE
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_TYPE);
	tmp = gconf_client_get_string (client, key, NULL);
	g_free (key);
	
	i = alarm_type_from_string (tmp);
	
	if (i > 0) {
		alarm->type = i;
	} else {
		// Not found in GConf, fall back to defaults
		alarm->type = ALARM_DEFAULT_TYPE;
		g_object_set (alarm, PROP_NAME_TYPE, ALARM_DEFAULT_TYPE, NULL);
	}
	
	if (tmp)
		g_free (tmp);
	
	/*
	 * TIME
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_TIME);
	val = gconf_client_get (client, key, NULL);
	g_free (key);
	
	if (val) {
		alarm->time = (time_t)gconf_value_get_int (val);
		gconf_value_free (val);
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_TIME, ALARM_DEFAULT_TIME, NULL);
	}
	
	/*
	 * ACTIVE
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_ACTIVE);
	val = gconf_client_get (client, key, NULL);
	g_free (key);
	
	if (val) {
		// We g_object_set here so the timer will be started for
		// active alarms
		g_object_set (alarm, "active", gconf_value_get_bool (val), NULL);
		gconf_value_free (val);
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_ACTIVE, ALARM_DEFAULT_ACTIVE, NULL);
	}
	
	/*
	 * MESSAGE
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_MESSAGE);
	tmp = gconf_client_get_string (client, key, NULL);
	g_free (key);
	
	if (tmp) {
		alarm->message = tmp;
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_MESSAGE, ALARM_DEFAULT_MESSAGE, NULL);
	}
	
	/*
	 * TIMER (storage for timer alarm type)
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_TIMER);
	val = gconf_client_get (client, key, NULL);
	g_free (key);
	
	if (val) {
		alarm->timer = (time_t)gconf_value_get_int (val);
		gconf_value_free (val);
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_TIMER, ALARM_DEFAULT_TIMER, NULL);
	}
	
	/*
	 * REPEAT
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_REPEAT);
	val = gconf_client_get (client, key, NULL);
	g_free (key);
	
	if (val) {
		list = alarm_gconf_extract_list_string (val);
		
		alarm->repeat = alarm_repeat_from_list (list);
		
		g_slist_free (list);
		gconf_value_free (val);
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_REPEAT, ALARM_DEFAULT_REPEAT, NULL);
	}
	
	/*
	 * NOTIFY TYPE
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_NOTIFY_TYPE);
	tmp = gconf_client_get_string (client, key, NULL);
	g_free (key);
	
	i = alarm_notify_type_from_string (tmp);
	
	if (i > 0) {
		alarm->notify_type = i;
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_NOTIFY_TYPE, ALARM_DEFAULT_NOTIFY_TYPE, NULL);
	}
	
	if (tmp)
		g_free (tmp);
	
	/*
	 * SOUND FILE
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_SOUND_FILE);
	tmp = gconf_client_get_string (client, key, NULL);
	g_free (key);
	
	if (tmp) {
		alarm->sound_file = tmp;
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_SOUND_FILE, ALARM_DEFAULT_SOUND_FILE, NULL);
	}
	
	/*
	 * SOUND LOOP
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_SOUND_LOOP);
	val = gconf_client_get (client, key, NULL);
	g_free (key);
	
	if (val) {
		alarm->sound_loop = gconf_value_get_bool (val);
		gconf_value_free (val);
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_SOUND_LOOP, ALARM_DEFAULT_SOUND_LOOP, NULL);
	}


	/*
	 * COMMAND
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_COMMAND);
	tmp = gconf_client_get_string (client, key, NULL);
	g_free (key);
	
	if (tmp) {
		alarm->command = tmp;
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_COMMAND, ALARM_DEFAULT_COMMAND, NULL);
	}
	
	
	/*
	 * NOTIFY_BUBBLE
	 */
	key = alarm_gconf_get_full_key (alarm, PROP_NAME_NOTIFY_BUBBLE);
	val = gconf_client_get (client, key, NULL);
	g_free (key);

	if (val) {
		alarm->notify_bubble = gconf_value_get_bool (val);
		gconf_value_free (val);
	} else {
		// Not found in GConf, fall back to defaults
		g_object_set (alarm, PROP_NAME_NOTIFY_BUBBLE, ALARM_DEFAULT_NOTIFY_BUBBLE, NULL);
	}
}

static void
alarm_init (Alarm *self)
{
	AlarmPrivate *priv = ALARM_PRIVATE (self);
	
	self->gconf_dir = NULL;
	self->id = -1;
	
	priv->gconf_listener = 0;
	priv->gconf_client = gconf_client_get_default ();
}

static void
alarm_constructed (GObject *object)
{
	Alarm *alarm = ALARM (object);
	
	// Load gconf settings
	alarm_gconf_load (alarm);
	
	// Connect gconf listener
	//alarm_gconf_connect (alarm);
}

/* set an Alarm property */
static void 
alarm_set_property (GObject *object, 
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec) 
{
	Alarm *alarm;
	AlarmPrivate *priv = ALARM_PRIVATE (object);
	
	GConfClient *client;
	GError 		*err = NULL;
	
	const gchar	*str;
	guint		 d, old;
	AlarmType 	 type, new_type;
	gboolean	 bool;
	GSList		*list;
	
	gchar *key, *tmp;

	alarm = ALARM (object);
	client = priv->gconf_client;
	
	switch (prop_id) {
	case PROP_DIR:
		str = g_value_get_string (value);
		
		/* Validate */
		if (!str) {
			g_critical ("Invalid gconf-dir value: \"%s\": NULL", str);
			return;
		}
		
		if (!gconf_valid_key (str, &tmp)) {
			g_critical ("Invalid gconf-dir value: \"%s\": %s", str, tmp);
			g_free (tmp);
			return;
		}
		
		if (!alarm->gconf_dir || strcmp (str, alarm->gconf_dir) != 0) {
			// Changed, remove old gconf listeners
			alarm_gconf_disconnect (alarm);
			
			if (alarm->gconf_dir != NULL)
				g_free (alarm->gconf_dir);
			alarm->gconf_dir = g_strdup (str);
			
			// Associate schemas
			alarm_gconf_associate_schemas (alarm);
			
			alarm_gconf_connect (alarm);
		}
		break;
	case PROP_ID:
		d = g_value_get_uint (value);
		
		if (d != alarm->id) {
			alarm_gconf_disconnect (alarm);
			alarm->id = d;
			
			alarm_gconf_associate_schemas (alarm);
			
			alarm_gconf_load (alarm);
			alarm_gconf_connect (alarm);
		}
		break;
	case PROP_TYPE:
		alarm->type = g_value_get_uint (value);
		
		// If we changed from CLOCK to TIMER we need to update the time
		if (alarm->type == ALARM_TYPE_TIMER && alarm->active) {
			/* If we're a TIMER, update the "time" property to now + timer */
			g_object_set (alarm, "time", time(NULL) + alarm->timer, NULL);
		}
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_TYPE);
		
		if (!gconf_client_set_string (client, key, 
									  alarm_type_to_string (alarm->type), 
									  &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		break;
	case PROP_TIME:
		alarm->time = g_value_get_uint (value);
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_TIME);
		
		if (!gconf_client_set_int (client, key, alarm->time, &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		break;
	case PROP_ACTIVE:
		bool = alarm->active;
		alarm->active = g_value_get_boolean (value);
		
		//g_debug ("[%p] #%d ACTIVE: old=%d new=%d", alarm, alarm->id, bool, alarm->active);
		if (alarm->active && !alarm_timer_is_started(alarm)) {
			if (alarm->type == ALARM_TYPE_TIMER) {
				/* If we're a TIMER, update the "time" property to now + timer */
				g_object_set (alarm, "time", time(NULL) + alarm->timer, NULL);
			}
			
			// Start timer
			alarm_timer_start (alarm);
		}
		else if (!alarm->active && alarm_timer_is_started(alarm)) {
			// Stop timer
			alarm_timer_remove (alarm);
		}
		
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_ACTIVE);
		
		if (!gconf_client_set_bool (client, key, alarm->active, &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		break;
	case PROP_MESSAGE:
		if (alarm->message)
			g_free (alarm->message);
		
		alarm->message = g_strdup (g_value_get_string (value));
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_MESSAGE);
		
		if (!gconf_client_set_string (client, key, 
									  alarm->message, 
									  &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		
		break;
	case PROP_TIMER:
		alarm->timer = g_value_get_uint (value);
		
		// If our alarm is active we need to update the time
		if (alarm->type == ALARM_TYPE_TIMER && alarm->active) {
			/* If we're a TIMER, update the "time" property to now + timer */
			g_object_set (alarm, "time", time(NULL) + alarm->timer, NULL);
		}
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_TIMER);
		
		if (!gconf_client_set_int (client, key, alarm->timer, &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		break;
	case PROP_REPEAT:
		alarm->repeat = g_value_get_uint (value);
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_REPEAT);
		list = alarm_repeat_to_list (alarm->repeat);
		
		if (!gconf_client_set_list(client, key, 
								   GCONF_VALUE_STRING, list, 
								   &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_slist_free (list);
		g_free (key);
		
		break;
	case PROP_NOTIFY_TYPE:
		alarm->notify_type = g_value_get_uint (value);
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_NOTIFY_TYPE);
		
		if (!gconf_client_set_string (client, key, 
									  alarm_notify_type_to_string (alarm->notify_type), 
									  &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		
		break;
	case PROP_SOUND_FILE:
		alarm->sound_file = g_strdup (g_value_get_string (value));
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_SOUND_FILE);
		
		if (!gconf_client_set_string (client, key, 
									  alarm->sound_file, 
									  &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		
		break;
	case PROP_SOUND_LOOP:
		alarm->sound_loop = g_value_get_boolean (value);
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_SOUND_LOOP);
		
		if (!gconf_client_set_bool (client, key, alarm->sound_loop, &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		break;
	case PROP_COMMAND:
		alarm->command = g_strdup (g_value_get_string (value));
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_COMMAND);
		
		if (!gconf_client_set_string (client, key, 
									  alarm->command, 
									  &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		
		break;
	case PROP_NOTIFY_BUBBLE:
		bool = alarm->notify_bubble;
		alarm->notify_bubble = g_value_get_boolean (value);
		
		key = alarm_gconf_get_full_key (alarm, PROP_NAME_NOTIFY_BUBBLE);
		
		if (!gconf_client_set_bool (client, key, alarm->notify_bubble, &err)) {
			
			g_critical ("Could not set %s (gconf): %s", 
						key, err->message);
			
			g_error_free (err);
		}
		
		g_free (key);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* retrive an Alarm property */
static void 
alarm_get_property (GObject *object, 
					guint prop_id, 
					GValue *value,
					GParamSpec *pspec) 
{
	Alarm *alarm = ALARM (object);
	
	switch (prop_id) {
	case PROP_DIR:
		g_value_set_string (value, alarm->gconf_dir);
		break;
	case PROP_ID:
		g_value_set_uint (value, alarm->id);
		break;
	case PROP_TYPE:
		g_value_set_uint (value, alarm->type);
		break;
	case PROP_TIME:
		g_value_set_uint (value, alarm->time);
		break;
	case PROP_ACTIVE:
		g_value_set_boolean (value, alarm->active);
		break;
	case PROP_MESSAGE:
		g_value_set_string (value, alarm->message);
		break;
	case PROP_TIMER:
		g_value_set_uint (value, alarm->timer);
		break;
	case PROP_REPEAT:
		g_value_set_uint (value, alarm->repeat);
		break;
	case PROP_NOTIFY_TYPE:
		g_value_set_uint (value, alarm->notify_type);
		break;
	case PROP_SOUND_FILE:
		g_value_set_string (value, alarm->sound_file);
		break;
	case PROP_SOUND_LOOP:
		g_value_set_boolean (value, alarm->sound_loop);
		break;
	case PROP_COMMAND:
		g_value_set_string (value, alarm->command);
		break;
	case PROP_NOTIFY_BUBBLE:
		g_value_set_boolean (value, alarm->notify_bubble);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/*
 * ERROR signal {{
 */
GQuark
alarm_error_quark (void)
{
	return g_quark_from_static_string ("alarm-error-quark");
}

static void
alarm_error (Alarm *alarm, GError *err)
{
	g_warning("[%p] #%d: alarm_error: #%d: %s", alarm, alarm->id, err->code, err->message);
}

void
alarm_error_trigger (Alarm *alarm, AlarmErrorCode code, const gchar *msg)
{
	GError *err = g_error_new (ALARM_ERROR, code, "%s", msg);
	
	g_signal_emit (alarm, alarm_signal[SIGNAL_ERROR], 0, err, NULL);
}

/*
 * }} ERROR signal
 */

static void
alarm_player_changed (Alarm *alarm, MediaPlayerState state)
{
	g_debug ("alarm_player_changed (%p, %d)", alarm, state);
}


/*
 * ALARM signal {{
 */

static void 
alarm_alarm (Alarm *alarm)
{
	g_debug ("[%p] #%d: alarm_alarm", alarm, alarm->id);
}

void
alarm_trigger (Alarm *alarm)
{
	g_signal_emit (alarm, alarm_signal[SIGNAL_ALARM], 0, NULL);
	
	/*
	 * Do we want to repeat this alarm?
	 */
	if (alarm_should_repeat (alarm)) {
		g_debug ("alarm_trigger REPEATING");
		struct tm *tm = localtime (&(alarm->time));
		alarm_set_time_full (alarm, tm->tm_hour, tm->tm_min, tm->tm_sec, alarm_repeat_next_wday (alarm->repeat, tm->tm_wday));
		
	} else {
		alarm_disable (alarm);
	}
	
	switch (alarm->notify_type) {
	case ALARM_NOTIFY_SOUND:
		// Start sound playback
		g_debug("[%p] #%d Start player", alarm, alarm->id);
		alarm_player_start (alarm);
		break;
	case ALARM_NOTIFY_COMMAND:
		// Start app
		g_debug("[%p] #%d Start command", alarm, alarm->id);
		alarm_command_run (alarm);
		break;
	default:
		g_debug ("NOTIFICATION TYPE %d Not yet implemented.", alarm->notify_type);
	}
}

/*
 * Convenience functions for enabling/disabling the alarm.
 */
void
alarm_enable (Alarm *alarm)
{
	g_object_set (alarm, "active", TRUE, NULL);
}

void
alarm_disable (Alarm *alarm)
{
	g_object_set (alarm, "active", FALSE, NULL);
}

/*
 * Delete alarm. This will remove all configuration
 * associated with this alarm.
 */
void
alarm_delete (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE (alarm);
	GConfClient *client = priv->gconf_client;
	gchar *key;
	
	// Disconnect gconf listeners
	alarm_gconf_disconnect (alarm);
	
	// Remove configuration
	key = alarm_gconf_get_dir (alarm);
	g_debug ("alarm_delete: recursive unset on %s", key);
	gconf_client_recursive_unset (client, key, GCONF_UNSET_INCLUDING_SCHEMA_NAMES, NULL);
	gconf_client_suggest_sync (client, NULL);
	g_free (key);
}

/*
 * Clear the alarm. This will stop any running players.
 */
void
alarm_clear (Alarm *alarm)
{
	alarm_player_stop (alarm);
}

/*
 * Is the alarm playing?
 */
gboolean
alarm_is_playing (Alarm *a)
{
	AlarmPrivate *priv = ALARM_PRIVATE (a);
	
	return priv->player && priv->player->state == MEDIA_PLAYER_PLAYING;
}



static gboolean
alarm_timer_update (Alarm *alarm)
{
	time_t now;
	
	time (&now);
	
	if (now >= alarm->time) {
		alarm_trigger (alarm);
		
		// Remove callback only if we don't intend to repeat the alarm
		return alarm_should_repeat (alarm);
	} else if (alarm->time - now <= 10) {
		g_debug ("[%p] #%d %2d...", alarm, alarm->id, (int)(alarm->time - now));
	}
	
	// Keep callback
	return TRUE;
}

static void
alarm_timer_start (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE(alarm);
	
	g_debug ("[%p] #%d timer_start", alarm, alarm->id);
	
	// Remove old timer, if any
	alarm_timer_remove (alarm);
	
	// Keep us updated every 1 second
	priv->timer_id = g_timeout_add_seconds (1, (GSourceFunc) alarm_timer_update, alarm);
}

static gboolean
alarm_timer_is_started (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE(alarm);
		
	return priv->timer_id > 0;
}

static void
alarm_timer_remove (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE(alarm);
	
	if (alarm_timer_is_started(alarm)) {
		g_debug ("#%d timer_remove (%p)", alarm->id, alarm);
		
		g_source_remove (priv->timer_id);
		
		priv->timer_id = 0;
	}
}


/*
 * }} ALARM signal
 */

/*
 * Taken from panel-applet.c
 */
static void
alarm_gconf_associate_schemas (Alarm *alarm)
{
	AlarmPrivate *priv  = ALARM_PRIVATE (alarm);
	GConfClient *client = priv->gconf_client;
	GSList *list, *l;
	GError *error = NULL;
	
	if (alarm->id < 0)
		return;

	list = gconf_client_all_entries (client, ALARM_GCONF_SCHEMA_DIR, &error);

	g_return_if_fail (error == NULL);

	for (l = list; l; l = l->next) {
		GConfEntry *entry = l->data;
		gchar	   *key;
		gchar	   *tmp;
		
		tmp = g_path_get_basename (gconf_entry_get_key (entry));
		
		if (strchr (tmp, '-'))
			g_warning ("Applet key '%s' contains a hyphen. Please "
					   "use underscores in gconf keys\n", tmp);
		
		key = alarm_gconf_get_full_key (alarm, tmp);

		g_free (tmp);
		
		gconf_engine_associate_schema (
				client->engine, key, gconf_entry_get_key (entry), &error);

		g_free (key);

		gconf_entry_free (entry);

		if (error) {
			g_slist_free (list);
			return;
		}
	}

	g_slist_free (list);
}

static void
alarm_gconf_connect (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE (alarm);
	gchar *dir;
	GError *err = NULL;
	
//	g_debug ("gconf_connect (%p) ? %d", alarm, IS_ALARM ((gpointer)alarm));
	
	if (alarm->id < 0)
		return;
	
	dir = alarm_gconf_get_dir (alarm);
	
//	g_debug ("alarm_gconf_connect (%p) to dir %s", alarm, dir);
	
	gconf_client_add_dir (priv->gconf_client, dir, 
						  GCONF_CLIENT_PRELOAD_ONELEVEL, &err);
	
	if (err) {
		g_warning ("alarm_gconf_connect (%p): gconf_client_add_dir (%s) failed: %s", alarm, dir, err->message);
		g_error_free (err);
		err = NULL;
	}
	
	priv->gconf_listener = 
		gconf_client_notify_add (
			priv->gconf_client, dir,
			(GConfClientNotifyFunc) alarm_gconf_dir_changed,
			alarm, NULL, &err);
	
	if (err) {
		g_warning ("alarm_gconf_connect (%p): gconf_client_notify_add (%s) failed: %s", alarm, dir, err->message);
		g_error_free (err);
		err = NULL;
	}
	
//	g_debug ("alarm_gconf_connect: Added listener %d to alarm #%d %p", priv->gconf_listener, alarm->id, alarm);
	
	g_free (dir);
}

static void
alarm_gconf_disconnect (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE (alarm);
	gchar *dir;
	
	if (priv->gconf_listener) {
		g_debug ("alarm_gconf_disconnect: Removing listener %d from alarm #%d %p", priv->gconf_listener, alarm->id, alarm);
		gconf_client_notify_remove (priv->gconf_client, priv->gconf_listener);
		priv->gconf_listener = 0;
		
		dir = alarm_gconf_get_dir (alarm);
		gconf_client_remove_dir (priv->gconf_client, dir, NULL);
		g_free (dir);
	}
}

/*
 * Updates the local copy with the new value if it has changed.
 */
static void 
alarm_gconf_dir_changed (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 gpointer data)
{
	Alarm *alarm = ALARM (data);
	GParamSpec *param;
	gchar *name;
	
	guint i;
	gboolean b;
	const gchar *str;
	GSList *list;
	
	name = g_path_get_basename (entry->key);
	param = g_object_class_find_property (G_OBJECT_GET_CLASS (alarm), name);
	
	if (!param) {
		g_free (name);
		return;
	}
	
	g_debug ("alarm_gconf changed #%d %p: %s", alarm->id, alarm, name);
	
	switch (param->param_id) {
	case PROP_TYPE:
		str = gconf_value_get_string (entry->value);
		i = alarm_type_from_string (str);
		if (i > 0 && i != alarm->type)
			g_object_set (alarm, name, i, NULL);
		break;
	case PROP_TIME:
		i = gconf_value_get_int (entry->value);
		if (i != alarm->time)
			g_object_set (alarm, name, i, NULL);
		break;
	case PROP_ACTIVE:
		b = gconf_value_get_bool (entry->value);
		if (b != alarm->active) {
			g_debug ("[%p] #%d GCONF-ACTIVE changed from %d to %d", alarm, alarm->id, alarm->active, b);
			g_object_set (alarm, name, b, NULL);
		}
		break;
	case PROP_MESSAGE:
		str = gconf_value_get_string (entry->value);
		if (strcmp (str, alarm->message) != 0)
			g_object_set (alarm, name, str, NULL);
		break;
	case PROP_TIMER:
		i = gconf_value_get_int (entry->value);
		if (i != alarm->timer)
			g_object_set (alarm, name, i, NULL);
		break;
	case PROP_REPEAT:
		list = alarm_gconf_extract_list_string (entry->value);
		
		i = alarm_repeat_from_list (list);
		if (i != alarm->repeat)
			g_object_set (alarm, name, i, NULL);
		
		g_slist_free (list);
		break;
	case PROP_NOTIFY_TYPE:
		str = gconf_value_get_string (entry->value);
		i = alarm_notify_type_from_string (str);
		if (i > 0 && i != alarm->notify_type)
			g_object_set (alarm, name, i, NULL);
		break;
	case PROP_SOUND_FILE:
		str = gconf_value_get_string (entry->value);
		if (strcmp (str, alarm->sound_file) != 0)
			g_object_set (alarm, name, str, NULL);
		break;
	case PROP_SOUND_LOOP:
		b = gconf_value_get_bool (entry->value);
		if (b != alarm->sound_loop)
			g_object_set (alarm, name, b, NULL);
		break;
	case PROP_COMMAND:
		str = gconf_value_get_string (entry->value);
		if (strcmp (str, alarm->command) != 0)
			g_object_set (alarm, name, str, NULL);
		break;
	case PROP_NOTIFY_BUBBLE:
		b = gconf_value_get_bool (entry->value);
		if (b != alarm->notify_bubble)
			g_object_set (alarm, name, b, NULL);
		break;
	default:
		g_warning ("Valid property ID %d not handled!", param->param_id);
		break;
	}
	
	g_free (name);
}

static void
alarm_dispose (GObject *object)
{
	Alarm *alarm = ALARM (object);
	AlarmPrivate *priv = ALARM_PRIVATE (object);
	GObjectClass *parent = (GObjectClass *)alarm_parent_class;
	
	g_debug ("alarm_dispose (%p)", alarm);
	
	if (parent->dispose)
		parent->dispose (object);
	
	alarm_gconf_disconnect (alarm);
}

/*
 * Convenience function for creating a new alarm instance.
 * Passing -1 as the id will generate a new ID with alarm_gen_id
 */
Alarm *
alarm_new (const gchar *gconf_dir, gint id)
{
	Alarm *alarm;
	
	if (id < 0) {
		id = alarm_gen_id_dir (gconf_dir);
	}
	
	alarm = g_object_new (TYPE_ALARM,
						  "gconf-dir", gconf_dir,
						  "id", id,
						  NULL);
	
	return alarm;
}

guint
alarm_gen_id_dir (const gchar *gconf_dir)
{
	GConfClient *client;
	gchar *key = NULL;
	gint id;
	
	client = gconf_client_get_default ();
	
	id = 0;
	do {
		if (key)
			g_free (key);
		
		key = g_strdup_printf("%s/" ALARM_GCONF_DIR_PREFIX "%d", gconf_dir, id);
		id++;
		
	} while (gconf_client_dir_exists (client, key, NULL));
	
	g_free (key);
	
	return (guint)(id-1);
}

/*
 * Will try to find the first available ID in gconf_dir
 */
guint
alarm_gen_id (Alarm *alarm)
{
	return alarm_gen_id_dir (alarm->gconf_dir);
}

const gchar *alarm_type_to_string (AlarmType type)
{
	return gconf_enum_to_string (alarm_type_enum_map, type);
}

AlarmType alarm_type_from_string (const gchar *type)
{
	AlarmType ret = ALARM_TYPE_INVALID;
	
	if (!type)
		return ret;
	
	gconf_string_to_enum (alarm_type_enum_map, type, (gint *)&ret);
	
	return ret;
}

const gchar *alarm_notify_type_to_string (AlarmNotifyType type)
{
	return gconf_enum_to_string (alarm_notify_type_enum_map, type);
}

AlarmNotifyType alarm_notify_type_from_string (const gchar *type)
{
	AlarmNotifyType ret = ALARM_NOTIFY_INVALID;
	
	if (!type)
		return ret;
	
	gconf_string_to_enum (alarm_notify_type_enum_map, type, (gint *)&ret);
	
	return ret;
}

gchar *
alarm_gconf_get_dir (Alarm *alarm)
{
	gchar *key;
	
	g_return_val_if_fail (IS_ALARM (alarm), NULL);
	
	key = g_strdup_printf ("%s/" ALARM_GCONF_DIR_PREFIX "%d", alarm->gconf_dir, alarm->id);
	
	return key;
}

gchar *
alarm_gconf_get_full_key (Alarm *alarm, const gchar *key)
{
	gchar *full_key;
	
	g_return_val_if_fail (IS_ALARM (alarm), NULL);

	if (!key)
		return NULL;
	
	full_key = g_strdup_printf ("%s/" ALARM_GCONF_DIR_PREFIX "%d/%s", alarm->gconf_dir, alarm->id, key);

	return full_key;
}

typedef struct {
	GObject *object;
	const gchar *name;
	gulong handler_id;
} AlarmBindArg;

static AlarmBindArg *
alarm_bind_arg_new (GObject *object, const gchar *name, gulong handler_id)
{
	AlarmBindArg *arg;
	
	arg = g_new (AlarmBindArg, 1);
	arg->object = object;
	arg->name = name;
	arg->handler_id = handler_id;
	
	return arg;
}

static void
alarm_bind_arg_free (AlarmBindArg *arg)
{
	if (!arg)
		return;
	
	g_free ((gpointer)arg);
}

static void
alarm_bind_update (GObject *object,
				   GParamSpec *pspec,
				   gpointer data)
{
	//Alarm *alarm;
	AlarmBindArg *arg = (AlarmBindArg *)data;
	gpointer d;
	
	/*
	 * Determine which argument is the alarm
	 */
	/*if (IS_ALARM (object)) {
		 alarm = ALARM (object);
	} else {
		alarm = ALARM (arg->object);
	}
	
	g_debug ("alarm_bind_update #%d(%p) [%s] -> %p [%s]", alarm->id, alarm, pspec->name, arg->object, arg->name);
	*/
	g_object_get (object, pspec->name, &d, NULL);
	
	// Block other signal handler
	g_signal_handlers_block_matched(arg->object, 
									G_SIGNAL_MATCH_FUNC,
									0, 0, NULL, alarm_bind_update, NULL);
	
	g_object_set (arg->object, arg->name, d, NULL);
	
	g_signal_handlers_unblock_matched(arg->object, 
									  G_SIGNAL_MATCH_FUNC,
									  0, 0, NULL, alarm_bind_update, NULL);
}

/*
 * We would like to disconnect any signal handlers from the alarm
 * when the destination object is finalized. And vice versa.
 */
static void
alarm_bind_weak_notify (gpointer data, GObject *where_the_object_was)
{
	AlarmBindArg *arg = (AlarmBindArg *)data;
	
	g_signal_handler_disconnect (arg->object, arg->handler_id);
	
	alarm_bind_arg_free (arg);
}

/*
 * Binds both ways.
 */
void
alarm_bind (Alarm *alarm, 
			const gchar *prop, 
			GObject *dest, 
			const gchar *dest_prop)
{
	AlarmBindArg *arg, *obj_arg;
	GParamSpec *param;
	gchar *tmp;
	gulong handler, obj_handler;
	
	param = g_object_class_find_property (G_OBJECT_GET_CLASS(alarm), prop);
	g_assert (param);
	param = g_object_class_find_property (G_OBJECT_GET_CLASS(dest), dest_prop);
	g_assert (param);
	
//	g_debug ("Bind from %p [%s] -> %p [%s]", alarm, prop, dest, dest_prop);
	
	// Alarm -> Object
	tmp = g_strdup_printf("notify::%s", prop);
	arg = alarm_bind_arg_new (dest, dest_prop, 0);
	handler = g_signal_connect (alarm, tmp, G_CALLBACK (alarm_bind_update), arg);
	g_free(tmp);
	
	// Object -> Alarm
	tmp = g_strdup_printf ("notify::%s", dest_prop);
	obj_arg = alarm_bind_arg_new (G_OBJECT (alarm), prop, handler);
	obj_handler = g_signal_connect (dest, tmp, G_CALLBACK (alarm_bind_update), obj_arg);
	g_free(tmp);
	
	arg->handler_id = obj_handler;
	
	// Disconnect Object when Alarm is finalized (freed)
	g_object_weak_ref (G_OBJECT (alarm), alarm_bind_weak_notify, arg);
	
	// Disconnect Alarm when Object is finalized (freed)
	g_object_weak_ref (dest, alarm_bind_weak_notify, obj_arg);
}

static gint
alarm_list_item_compare (gconstpointer a, gconstpointer b)
{
	return strcmp ((const gchar *)a, (const gchar *)b);
}

/*
 * Check if a gconf directory is a valid alarm dir.
 * 
 * Returns >= 0 for valid alarm directory, -1 otherwise.
 */
gint
alarm_gconf_dir_get_id (const gchar *dir)
{
	gint id;
	gchar *d;
	
	d = g_path_get_basename (dir);
	
	if (sscanf (d, ALARM_GCONF_DIR_PREFIX "%d", &id) <= 0 || id < 0) {
		// INVALID
		id = -1;
	}
	
	g_free (d);
	
	return id;
}

/*
 * Get list of alarms in gconf_dir
 */
GList *
alarm_get_list (const gchar *gconf_dir)
{
	GConfClient *client;
	GSList *dirs = NULL;
	GSList *l = NULL;
	gchar *tmp, *dir;
	
	GList *ret = NULL;
	Alarm *alarm;
	gint id;
	
	client = gconf_client_get_default ();
	dirs = gconf_client_all_dirs (client, gconf_dir, NULL);
	
	if (!dirs)
		return NULL;
	
	dirs = g_slist_sort (dirs, alarm_list_item_compare);
	
	for (l = dirs; l; l = l->next) {
		tmp = (gchar *)l->data;
		
		id = alarm_gconf_dir_get_id (tmp);
		if (id >= 0) {
			g_debug ("alarm_get_list: found VALID %s #%d", tmp, id);
			
			alarm = alarm_new (gconf_dir, id);
			g_debug ("\tref = %d", G_OBJECT (alarm)->ref_count);
			ret = g_list_append (ret, alarm);
			g_debug ("\tappend ref = %d", G_OBJECT (alarm)->ref_count);
		}
		
		g_free (tmp);
	}
	
	g_slist_free (dirs);
	
	return ret;
}

/*
 * Connect a signal callback to all alarms in list.
 */
void
alarm_signal_connect_list (GList *instances,
						   const gchar *detailed_signal,
						   GCallback c_handler,
						   gpointer data)
{
	GList *l;
	Alarm *a;
	g_debug ("alarm_signal_connect_list");
	for (l = instances; l != NULL; l = l->next) {
		a = ALARM (l->data);
		
		g_debug (" - connecting #%d: %s...", a->id, detailed_signal);
		
		gboolean ret = g_signal_connect (a, detailed_signal, c_handler, data);
		g_debug (" = %d", ret);
	}
}


/**
 * Player error
 */
static void
alarm_player_error_cb (MediaPlayer *player, GError *err, gpointer data)
{
	Alarm *alarm = ALARM (data);
	gchar *uri;
	gchar *msg;
	
	uri = media_player_get_uri (player);
	msg = g_strdup_printf ("Could not play '%s': %s", uri, err->message);
	
	g_critical (msg);
	
	/* Emit error signal */
	alarm_error_trigger (alarm, ALARM_ERROR_PLAY, msg);
	
	g_free (uri);
	g_free (msg);
}

static void
alarm_player_state_cb (MediaPlayer *player, MediaPlayerState state, gpointer data)
{
	static MediaPlayerState prev_state = MEDIA_PLAYER_INVALID;
	
	Alarm *alarm	   = ALARM (data);
	AlarmPrivate *priv = ALARM_PRIVATE (alarm);
	
	if (state != prev_state) {
		// Emit player_changed signal
		g_signal_emit (alarm, alarm_signal[SIGNAL_PLAYER], 0, state, NULL);
	}
	
	if (state == MEDIA_PLAYER_STOPPED) {
		g_debug ("[%p] #%d Freeing media player %p", alarm, alarm->id, player);
		
		media_player_free (player);
		
		priv->player = NULL;
	}
}


/**
 * Play sound via gstreamer
 */
static void
alarm_player_start (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE (alarm);
	
	if (priv->player == NULL) {
		priv->player = media_player_new (alarm->sound_file,
										 alarm->sound_loop,
										 alarm_player_state_cb, alarm,
										 alarm_player_error_cb, alarm);
	} else {
		media_player_set_uri (priv->player, alarm->sound_file);
	}
	
	media_player_start (priv->player);
	
	g_debug ("[%p] #%d player_start...", alarm, alarm->id);
}

/**
 * Stop player
 */
static void
alarm_player_stop (Alarm *alarm)
{
	AlarmPrivate *priv = ALARM_PRIVATE (alarm);
	
	if (priv->player != NULL) {
		media_player_stop (priv->player);
	}
}

/*
 * Run Command
 */
static void
alarm_command_run (Alarm *alarm)
{
	GError *err = NULL;
	gchar *msg;
	
	if (!g_spawn_command_line_async (alarm->command, &err)) {
		
		msg = g_strdup_printf ("Could not launch `%s': %s", alarm->command, err->message);
		
		g_critical (msg);
		
		/* Emit error signal */
		alarm_error_trigger (alarm, ALARM_ERROR_COMMAND, msg);
		
		g_free (msg);
		g_error_free (err);
	}
}

/*
 * Calculates the distance from wday1 to wday2
 */
gint
alarm_wday_distance (gint wday1, gint wday2)
{
	gint d;
	
	d = wday2 - wday1;
	if (d < 0)
		d += 7;
	
	return d;
}

/*
 * Set time according to hour, min, sec and wday
 * 
 * wday=-1 equals closest matching day (today or tomorrow depending on hour:min:sec)
 * wday=7  equals in a week
 */
void
alarm_set_time_full (Alarm *alarm, guint hour, guint minute, guint second, gint wday)
{
	time_t now, new;
	gint d;
	struct tm *tm;
	
	time (&now);
	tm = localtime (&now);
	
	g_assert (wday <= 7 && wday >= -1);
	
	if (wday < 0 || wday == tm->tm_wday) {
		// Check if the alarm is for tomorrow
		if (hour < tm->tm_hour ||
			(hour == tm->tm_hour && minute < tm->tm_min) ||
			(hour == tm->tm_hour && minute == tm->tm_min && second < tm->tm_sec)) {
			
			if (wday < 0) {
				g_debug("alarm_set_time_full: Alarm is for tomorrow.");
				tm->tm_mday++;
			} else {
				// wday == tm->tm_wday
				g_debug("alarm_set_time_full: Alarm is in 1 week.");
				tm->tm_mday += 7;
			}
		}
	} else {
		// Calculate distance from now to wday
		if (wday == 7) {
			g_debug("alarm_set_time_full: Alarm is in (forced) 1 week.");
			d = 7;
		} else {
			g_debug ("d = tm->tm_wday(%d) - wday(%d)", tm->tm_wday, wday);
			d = alarm_wday_distance (tm->tm_wday, wday);
		}
		
		g_debug ("alarm_set_time_full: Alarm is in %d days.", d);
		
		tm->tm_mday += d;
	}
	
	tm->tm_hour = hour;
	tm->tm_min  = minute;
	tm->tm_sec  = second;
	
	// DEBUG:
	char tmp[512];
	strftime (tmp, sizeof (tmp), "%c", tm);
	g_debug ("alarm_set_time_full: Alarm will trigger at %s", tmp);
	
	new = mktime (tm);
	g_debug ("alarm_set_time_full: Setting to %d", new);
	g_object_set (alarm, "time", new, NULL);
}

/*
 * Set time according to hour, min, sec
 */
void
alarm_set_time (Alarm *alarm, guint hour, guint minute, guint second)
{
	alarm_set_time_full (alarm, hour, minute, second, -1);
}

/*
 * Set timer according to hour, min, sec
 */
void
alarm_set_timer (Alarm *alarm, guint hour, guint minute, guint second)
{
	time_t timer = second + minute * 60 + hour * 60 * 60;
	
	g_object_set (alarm, "timer", timer, NULL);
}

/*
 * Update the alarm time to point to the nearest future
 * hour/min/sec according to the previous timestamp.
 */
void
alarm_update_time (Alarm *alarm)
{
	time_t now;
	struct tm *tm;
	
	time (&now);
	
	if (now > alarm->time) {
		g_debug ("update_time: %d > %d", now, alarm->time);
		tm = localtime (&(alarm->time));
		g_debug ("update_time: %d > %d", now, alarm->time);
		alarm_set_time(alarm, tm->tm_hour, tm->tm_min, tm->tm_sec);
	}
}

/*
 * Get the alarm time.
 */
struct tm *
alarm_get_time (Alarm *alarm)
{
	if (alarm->type == ALARM_TYPE_TIMER) {
		return gmtime (&(alarm->timer));
	}
	
	return localtime (&(alarm->time));
}

/*
 * Get the remaining alarm time.
 */
static struct tm tm;

struct tm *
alarm_get_remain (Alarm *alarm)
{
	time_t now;
	//struct tm tm;
	
	now = time (NULL);
	tm.tm_sec = alarm->time - now;
	
	tm.tm_min = tm.tm_sec / 60;
	tm.tm_sec -= tm.tm_min * 60;

	tm.tm_hour = tm.tm_min / 60;
	tm.tm_min -= tm.tm_hour * 60;
	
	return &tm;
}

/*
 * AlarmRepeat utilities {{
 */

const gchar *
alarm_repeat_to_string (AlarmRepeat repeat)
{
	return gconf_enum_to_string (alarm_repeat_enum_map, repeat);
}

AlarmRepeat
alarm_repeat_from_string (const gchar *str)
{
	AlarmRepeat ret = ALARM_REPEAT_NONE;
	
	if (!str)
		return ret;
	
	gconf_string_to_enum (alarm_repeat_enum_map, str, (gint *)&ret);
	
	return ret;
}

AlarmRepeat
alarm_repeat_from_list (GSList *list)
{
	GSList *l;
	AlarmRepeat repeat = ALARM_REPEAT_NONE;
	
	for (l = list; l; l = l->next) {
		repeat |= alarm_repeat_from_string ((gchar *)l->data);
	}
	
	return repeat;
}

GSList *
alarm_repeat_to_list (AlarmRepeat repeat)
{
	GSList *list = NULL;
	AlarmRepeat r;
	gint i;
	
	for (r = ALARM_REPEAT_SUN, i = 0; r <= ALARM_REPEAT_SAT; r = 1 << ++i) {
		if (repeat & r)
			list = g_slist_append (list, (gpointer) alarm_repeat_to_string (r));
	}
	
	return list;
}



/*
 * Get the next week day in repeat
 * 
 * repeat = AlarmRepeat bitmap
 * today  = The wday today, sun = 0
 */
gint
alarm_repeat_next_wday (AlarmRepeat repeat, gint today)
{
	gint i, wday = -1;
	AlarmRepeat rep;
	
	//i = (today == 6) ? 0 : today + 1;
	//today--;
	
	// Try finding a day in this week
	for (i = today + 1; i < 7; i++) {
		rep = 1 << i;
		//g_debug ("alarm_repeat_next_wday: this week { rep=%d & %d }", repeat, rep);
		if (repeat & rep) {
			// FOUND!
			//g_debug ("\tMATCH");
			wday = i;
			break;
		}
	}
	
	// If we haven't found a day in the current week, check next week
	if (wday == -1) {
		for (i = 0; i <= today; i++) {
			rep = 1 << i;
			//g_debug ("alarm_repeat_next_wday: next week { rep=%d & %d }", repeat, rep);
			if (repeat & rep) {
				// FOUND!
				//g_debug ("\tMATCH");
				wday = i;
				break;
			}
		}
	}
	
	if (wday == today)
		wday = 7;
	
	return wday;
}

gboolean
alarm_should_repeat (Alarm *alarm)
{
	return alarm->type == ALARM_TYPE_CLOCK && alarm->repeat != ALARM_REPEAT_NONE;
}

/*
 * }} AlarmRepeat utilities
 */
