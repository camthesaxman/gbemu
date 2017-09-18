#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include <time.h>
#include <unistd.h>
#ifndef CLOCK_MONOTONIC
#warning CLOCK_MONOTONIC is not defined
#define CLOCK_MONOTONIC 1
#endif
#include <gtk/gtk.h>

#include "../global.h"
#include "../config.h"
#include "../gameboy.h"
#include "platform.h"

GtkWidget *window = NULL;
GtkWidget *screenImage;
GdkPixbuf *pixbuf;

// Menus
static GtkWidget *mainMenu;
static GtkWidget *fileMenu;
static GtkWidget *fileOpenItem;
static GtkWidget *fileCloseItem;
static GtkWidget *fileExitItem;
static GtkWidget *emulationMenu;
static GtkWidget *emulationResetItem;
static GtkWidget *emulationPauseItem;
static GtkWidget *emulationResumeItem;
static GtkWidget *emulationStepFrameItem;
static GtkWidget *viewMenu;
static GtkWidget *optionsMenu;
static GtkWidget *optionsConfigureKeys;

static char currentRomName[256];
static bool exitApp = false;
static bool isRomLoaded = false;
static bool isRunning = false;
static uint8_t frameBufferPixels[GB_DISPLAY_WIDTH * GB_DISPLAY_HEIGHT];
static uint8_t palette[][3] =
{
    {255, 255, 255},
    {160, 160, 160},
    {80,  80,  80},
    {0,   0,   0},
};

void platform_fatal_error(char *fmt, ...)
{
    va_list args;
    char buffer[1000];
    GtkWidget *dialog;
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    dialog = gtk_message_dialog_new(GTK_WINDOW(window),
      window != NULL ? GTK_DIALOG_MODAL : 0, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_OK, buffer);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    va_end(args);
    config_save("gbemu_cfg.txt");
    exit(1);
}

void platform_error(char *fmt, ...)
{
    va_list args;
    char buffer[1000];
    GtkWidget *dialog;
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    dialog = gtk_message_dialog_new(GTK_WINDOW(window),
      window != NULL ? GTK_DIALOG_MODAL : 0, GTK_MESSAGE_ERROR,
      GTK_BUTTONS_OK, buffer);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    va_end(args);
}

uint8_t *platform_get_framebuffer(void)
{
    return frameBufferPixels;
}

static void render_framebuffer(void)
{
    uint8_t *rgb = gdk_pixbuf_get_pixels(pixbuf);
    
    // Convert from color indexed to RGB
    for (int i = 0; i < GB_DISPLAY_WIDTH * GB_DISPLAY_HEIGHT; i++)
    {
        *(rgb++) = palette[frameBufferPixels[i]][0];
        *(rgb++) = palette[frameBufferPixels[i]][1];
        *(rgb++) = palette[frameBufferPixels[i]][2];
    }
    gtk_widget_queue_draw(screenImage);
}

void platform_draw_done(void)
{
    render_framebuffer();
}

//------------------------------------------------------------------------------
// ROM handling functions
//------------------------------------------------------------------------------

static void try_load_rom(const char *filename)
{
    if (gameboy_load_rom(filename))
    {
        strcpy(currentRomName, filename);
        isRomLoaded = true;
        isRunning = true;
        gtk_widget_set_sensitive(fileCloseItem, TRUE);
        gtk_widget_set_sensitive(emulationResetItem, TRUE);
        gtk_widget_set_sensitive(emulationPauseItem, TRUE);
        gtk_widget_set_sensitive(emulationResumeItem, FALSE);
        gtk_window_set_title(GTK_WINDOW(window), APPNAME" (running)");
    }
    else
        platform_error("Failed to load ROM '%s'", filename);
}

static void close_game(void)
{
    gameboy_close_rom();
    memset(frameBufferPixels, 0, sizeof(frameBufferPixels));
    render_framebuffer();
    isRomLoaded = false;
    isRunning = false;
    gtk_widget_set_sensitive(fileCloseItem, FALSE);
    gtk_widget_set_sensitive(emulationResetItem, FALSE);
    gtk_widget_set_sensitive(emulationPauseItem, FALSE);
    gtk_widget_set_sensitive(emulationResumeItem, FALSE);
    gtk_widget_set_sensitive(emulationStepFrameItem, FALSE);
    gtk_window_set_title(GTK_WINDOW(window), APPNAME);
}

static void pause_game(void)
{
    isRunning = false;
    gtk_widget_set_sensitive(emulationPauseItem, FALSE);
    gtk_widget_set_sensitive(emulationResumeItem, TRUE);
    gtk_widget_set_sensitive(emulationStepFrameItem, TRUE);
    gtk_window_set_title(GTK_WINDOW(window), APPNAME" (paused)");
}

static void resume_game(void)
{
    isRunning = true;
    gtk_widget_set_sensitive(emulationPauseItem, TRUE);
    gtk_widget_set_sensitive(emulationResumeItem, FALSE);
    gtk_widget_set_sensitive(emulationStepFrameItem, FALSE);
    gtk_window_set_title(GTK_WINDOW(window), APPNAME" (running)");
}

//------------------------------------------------------------------------------
// Key Configuration Dialog
//------------------------------------------------------------------------------

static gboolean edit_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer userData)
{
    unsigned int *pKeyConfig = (unsigned int *)userData;
    
    *pKeyConfig = event->key.keyval;
    gtk_entry_set_text(GTK_ENTRY(widget), gdk_keyval_name(*pKeyConfig));
    return TRUE;
}

static void add_key_editor(GtkWidget *table, unsigned int row, const char *label, unsigned int *pKeyConfig)
{
    GtkWidget *entry = gtk_entry_new();
    
    gtk_entry_set_text(GTK_ENTRY(entry), gdk_keyval_name(*pKeyConfig));
    g_signal_connect(entry, "key-press-event", G_CALLBACK(edit_key_press_event), pKeyConfig);
    gtk_table_attach(GTK_TABLE(table), gtk_label_new(label),
      0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach(GTK_TABLE(table), entry,
      1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
}

static void open_key_config_dialog(void)
{
    struct ConfigKeys newKeys = gConfig.keys;
    GtkWidget *dialog;
    GtkWidget *contentArea;
    GtkWidget *hbox;
    GtkWidget *dpadFrame;
    GtkWidget *dpadTable;
    GtkWidget *buttonFrame;
    GtkWidget *buttonTable;
    GtkWidget *editA;
    GtkWidget *editB;
    GtkWidget *editStart;
    GtkWidget *editSelect;
    GtkWidget *editUp;
    GtkWidget *editDown;
    GtkWidget *editLeft;
    GtkWidget *editRight;
    
    dialog = gtk_dialog_new_with_buttons("Key Configuration", GTK_WINDOW(window), 0,
      GTK_STOCK_OK, GTK_RESPONSE_OK,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_APPLY, GTK_RESPONSE_APPLY,
      NULL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    hbox = gtk_hbox_new(TRUE, 0);
    dpadFrame = gtk_frame_new("D-Pad");
    dpadTable = gtk_table_new(4, 2, FALSE);
    add_key_editor(dpadTable, 0, "Up", &newKeys.up);
    add_key_editor(dpadTable, 1, "Down", &newKeys.down);
    add_key_editor(dpadTable, 2, "Left", &newKeys.left);
    add_key_editor(dpadTable, 3, "Right", &newKeys.right);
    gtk_container_add(GTK_CONTAINER(dpadFrame), dpadTable);
    buttonFrame = gtk_frame_new("Buttons");
    buttonTable = gtk_table_new(4, 2, FALSE);
    add_key_editor(buttonTable, 0, "A", &newKeys.a);
    add_key_editor(buttonTable, 1, "B", &newKeys.b);
    add_key_editor(buttonTable, 2, "Start", &newKeys.start);
    add_key_editor(buttonTable, 3, "Select", &newKeys.select);
    gtk_container_add(GTK_CONTAINER(buttonFrame), buttonTable);
    gtk_box_pack_start(GTK_BOX(hbox), dpadFrame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), buttonFrame, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(contentArea), hbox);
    gtk_widget_show_all(dialog);
    while (1)
    {
        switch (gtk_dialog_run(GTK_DIALOG(dialog)))
        {
          case GTK_RESPONSE_OK:
            gConfig.keys = newKeys;
            goto done;
          case GTK_RESPONSE_CANCEL:
            goto done;
          case GTK_RESPONSE_APPLY:
            gConfig.keys = newKeys;
            break;
        }
    }
  done:
    gtk_widget_destroy(dialog);
}

//------------------------------------------------------------------------------
// Widget Callbacks
//------------------------------------------------------------------------------

// File Menu

static void menu_file_open_activate(GtkMenuItem *menuItem, gpointer userData)
{
    GtkWidget *dialog;
    
    dialog = gtk_file_chooser_dialog_new("Open", GTK_WINDOW(window), GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
      NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        char *filename;
        
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        try_load_rom(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void menu_file_close_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    close_game();
}

static void menu_file_exit_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    exitApp = true;
}

// Emulation menu

static void menu_emulation_reset_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    close_game();
    try_load_rom(currentRomName);
}

static void menu_emulation_pause_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    pause_game();
}

static void menu_emulation_resume_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    resume_game();
}

static void menu_emulation_step_frame_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    gameboy_run_frame();
}

static void menu_options_configure_keys_activate(GtkMenuItem *menuItem, gpointer userData)
{
    UNUSED(menuItem);
    UNUSED(userData);
    open_key_config_dialog();
}

static GtkWidget *add_menubar_submenu(GtkWidget *menuBar, const char *label)
{
    GtkWidget *menu;
    GtkWidget *menuItem;
    
    menu = gtk_menu_new();
    menuItem = gtk_menu_item_new_with_label(label);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuItem), menu);
    gtk_menu_bar_append(GTK_MENU_BAR(menuBar), menuItem);
    return menu;
}

static GtkWidget *append_to_menu(GtkWidget *menu, const char *label, void (*callback)(GtkMenuItem *, gpointer))
{
    GtkWidget *menuItem;
    
    if (label == NULL)
        menuItem = gtk_separator_menu_item_new();
    else
    {
        menuItem = gtk_menu_item_new_with_label(label);
        g_signal_connect(menuItem, "activate", G_CALLBACK(callback), NULL);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuItem);
    return menuItem;
}

static void create_menu_bar(void)
{    
    mainMenu = gtk_menu_bar_new();
    
    fileMenu = add_menubar_submenu(mainMenu, "File");
    fileOpenItem = append_to_menu(fileMenu, "Open", menu_file_open_activate);
    fileCloseItem = append_to_menu(fileMenu, "Close", menu_file_close_activate);
    append_to_menu(fileMenu, NULL, NULL);
    fileExitItem = append_to_menu(fileMenu, "Exit", menu_file_exit_activate);
    
    emulationMenu = add_menubar_submenu(mainMenu, "Emulation");
    emulationResetItem = append_to_menu(emulationMenu, "Reset", menu_emulation_reset_activate);
    emulationPauseItem = append_to_menu(emulationMenu, "Pause", menu_emulation_pause_activate);
    emulationResumeItem = append_to_menu(emulationMenu, "Resume", menu_emulation_resume_activate);
    emulationStepFrameItem = append_to_menu(emulationMenu, "Step Frame", menu_emulation_step_frame_activate);
    
    viewMenu = add_menubar_submenu(mainMenu, "View");
    
    optionsMenu = add_menubar_submenu(mainMenu, "Options");
    optionsConfigureKeys = append_to_menu(optionsMenu, "Configure Keys", menu_options_configure_keys_activate);
}

static gboolean window_key_press_event(GtkWidget *widget, GdkEvent *event, gpointer userData)
{
    unsigned int key = event->key.keyval;
    
    if      (key == gConfig.keys.a)
        gameboy_joypad_press(KEY_A_BUTTON);
    else if (key == gConfig.keys.b)
        gameboy_joypad_press(KEY_B_BUTTON);
    else if (key == gConfig.keys.start)
        gameboy_joypad_press(KEY_START_BUTTON);
    else if (key == gConfig.keys.select)
        gameboy_joypad_press(KEY_SELECT_BUTTON);
    else if (key == gConfig.keys.up)
        gameboy_joypad_press(KEY_DPAD_UP);
    else if (key == gConfig.keys.down)
        gameboy_joypad_press(KEY_DPAD_DOWN);
    else if (key == gConfig.keys.left)
        gameboy_joypad_press(KEY_DPAD_LEFT);
    else if (key == gConfig.keys.right)
        gameboy_joypad_press(KEY_DPAD_RIGHT);
    
    return FALSE;
}

static gboolean window_key_release_event(GtkWidget *widget, GdkEvent *event, gpointer userData)
{
    unsigned int key = event->key.keyval;
    
    if      (key == gConfig.keys.a)
        gameboy_joypad_release(KEY_A_BUTTON);
    else if (key == gConfig.keys.b)
        gameboy_joypad_release(KEY_B_BUTTON);
    else if (key == gConfig.keys.start)
        gameboy_joypad_release(KEY_START_BUTTON);
    else if (key == gConfig.keys.select)
        gameboy_joypad_release(KEY_SELECT_BUTTON);
    else if (key == gConfig.keys.up)
        gameboy_joypad_release(KEY_DPAD_UP);
    else if (key == gConfig.keys.down)
        gameboy_joypad_release(KEY_DPAD_DOWN);
    else if (key == gConfig.keys.left)
        gameboy_joypad_release(KEY_DPAD_LEFT);
    else if (key == gConfig.keys.right)
        gameboy_joypad_release(KEY_DPAD_RIGHT);
    
    return FALSE;
}

static gboolean window_delete_event(GtkWidget *widget, GdkEvent *event, gpointer userData)
{
    UNUSED(widget);
    UNUSED(event);
    UNUSED(userData);
    exitApp = true;
    return false;
}

int main(int argc, char **argv)
{
    GtkWidget *vbox;
    unsigned long int ticks;
    unsigned long int newTicks;
    struct timespec ts;
    
    gtk_init(&argc, &argv);
    config_load("gbemu_cfg.txt");
    create_menu_bar();
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (window == NULL)
        platform_fatal_error("Failed to create window.");
    gtk_window_set_title(GTK_WINDOW(window), APPNAME);
    gtk_widget_set_size_request(window, gConfig.windowWidth, gConfig.windowHeight);
    g_signal_connect(window, "key-press-event", G_CALLBACK(window_key_press_event), NULL);
    g_signal_connect(window, "key-release-event", G_CALLBACK(window_key_release_event), NULL);
    g_signal_connect(window, "delete-event", G_CALLBACK(window_delete_event), NULL);
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), mainMenu, FALSE, FALSE, 0);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
      GB_DISPLAY_WIDTH, GB_DISPLAY_HEIGHT);
    screenImage = gtk_image_new_from_pixbuf(pixbuf);
    gtk_box_pack_start(GTK_BOX(vbox), screenImage, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    if (argc >= 2)
        try_load_rom(argv[1]);
    gtk_widget_show_all(window);
    
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ticks = ts.tv_nsec / 1000000;
    while (!exitApp)
    {
        if (isRunning)
        {            
            gtk_main_iteration_do(FALSE);
            gameboy_run_frame();
            clock_gettime(CLOCK_MONOTONIC, &ts);
            newTicks = ts.tv_nsec / 1000000;
            if (newTicks < ticks + 1000 / 60)
                usleep((ticks + 1000 / 60 - newTicks) * 1000);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ticks = ts.tv_nsec / 1000000;
        }
        else
        {
            gtk_main_iteration();
        }
    }
  config_save("gbemu_cfg.txt");
    return 0;
}
