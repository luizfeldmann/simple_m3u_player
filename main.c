/* ===================================================================================  //
//    This program is free software: you can redistribute it and/or modify              //
//    it under the terms of the GNU General Public License as published by              //
//    the Free Software Foundation, either version 3 of the License, or                 //
//    (at your option) any later version.                                               //
//                                                                                      //
//    This program is distributed in the hope that it will be useful,                   //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of                    //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                     //
//    GNU General Public License for more details.                                      //
//                                                                                      //
//    You should have received a copy of the GNU General Public License                 //
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.            //
//                                                                                      //
//    Copyright: Luiz Gustavo Pfitscher e Feldmann, 2020                                //
// ===================================================================================  */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <gtk/gtk.h>    // sudo apt install libgtk-3-dev
#include <gdk/gdkx.h>
#include <curl/curl.h>  // sudo apt install libcurl4-openssl-dev
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <vlc/vlc.h>    // sudo apt install libvlc-dev
#include <ctype.h>
//#include <X11/Xlib.h>   // sudo apt install libx11-dev

// =====================================
// PLAYLIST
// =====================================

typedef struct playlist_entry {
    char* url;
    char* name;
    char* logo;
    void* private_data;
} playlist_entry_t;

typedef struct playlist_group {
    char* group_name;
    uint16_t num_entries;
    playlist_entry_t* entries;
} playlist_group_t;

typedef struct playlist {
    uint8_t num_groups;
    playlist_group_t* groups;
} playlist_t;

playlist_group_t* playlist_find_group(playlist_t* playlist, const char* group_name)
{
    if (playlist == NULL || group_name == NULL) // sanity check
        return NULL;

    for (uint8_t g = 0; g < playlist->num_groups; g++)
        if (strcmp(playlist->groups[g].group_name, group_name) == 0)
            return &playlist->groups[g];

    return NULL;
}

playlist_group_t* playlist_new_group(playlist_t* playlist, const char* group_name)
{
    if (playlist == NULL || group_name == NULL) // sanity check
        return NULL;

    // alloc space for new group struct in the playlist
    playlist_group_t* new_list = (playlist_group_t*)realloc(playlist->groups, sizeof(playlist_group_t)*(playlist->num_groups + 1));
    if (new_list == NULL)
        return NULL;
    else
        playlist->groups = new_list;

    // setup group
    playlist_group_t* new_group = &playlist->groups[playlist->num_groups++];
    new_group->num_entries = 0;
    new_group->entries = NULL;
    new_group->group_name = malloc(strlen(group_name) + 1);
    strcpy(new_group->group_name, group_name);

    return new_group;
}

playlist_entry_t* group_new_entry(playlist_group_t* g, const char* name, const char* logo, const char* url)
{
    if (g == NULL || name == NULL  || logo == NULL || url == NULL) // sanity check
        return NULL;

    // allocate space in list for new entry
    playlist_entry_t* new_pl = (playlist_entry_t*)realloc(g->entries, (g->num_entries + 1)*sizeof(playlist_entry_t));
    if (new_pl == NULL) // could not reallocate
        return NULL;
    else
        g->entries = new_pl;

    playlist_entry_t* new_entry = &g->entries[g->num_entries++];

    // alloc space for the content
    uint16_t len_name, len_logo, len_url;

    *new_entry = (playlist_entry_t) {
        .name  = malloc( (len_name =  strlen(name) + 1) ),
        .logo  = malloc( (len_logo =  strlen(logo) + 1) ),
        .url   = malloc( (len_url =   strlen(url) + 1) ),
        .private_data = NULL,
    };

    // copy the content from temp buffer to inside the (permanente) new list entry
    memcpy(new_entry->name,  name,  len_name);
    memcpy(new_entry->logo,  logo,  len_logo);
    memcpy(new_entry->url,   url,   len_url);

    return new_entry;
}

playlist_entry_t* playlist_new_entry(playlist_t* playlist, const char* group_name, const char* name, const char* logo, const char* url)
{
    if (playlist == NULL || group_name == NULL) // sanity check
        return NULL;

    // find a group or create one
    playlist_group_t* gro;

    if ((gro = playlist_find_group(playlist, group_name)) == NULL)    // try to find group
        if ((gro = playlist_new_group(playlist, group_name)) == NULL) // try to create group
            return NULL; // something went wrong...

    return group_new_entry(gro, name, logo, url);
}

void playlist_entry_destroy(playlist_entry_t* entry)
{
    if (entry == NULL) // sanity check
        return;

    free(entry->name);
    free(entry->logo);
    free(entry->url);
}

void playlist_group_destroy(playlist_group_t* gro)
{
    if (gro == NULL || gro->entries == NULL) // sanity check
        return;

    for (uint16_t e = 0; e < gro->num_entries; e++)
        playlist_entry_destroy(&gro->entries[e]);

    free(gro->entries);
}

void playlist_destroy(playlist_t* playlist)
{
    if (playlist == NULL || playlist->groups == NULL) // sanity check
        return;

    for (uint8_t g = 0; g < playlist->num_groups; g++)
        playlist_group_destroy(&playlist->groups[g]);

    free(playlist->groups);
}

uint16_t read_playlist(const char* filename, playlist_t* playlist)
{
    // sanity check
    if (filename == NULL || playlist == NULL)
        return 0;
    else
    {
        // fresh start
        playlist->num_groups = 0;
        playlist->groups = NULL;
    }

    // open the file
    FILE* fp;
    if ((fp = fopen(filename, "rb+")) == NULL)
        return 0;

    // parse file
    uint16_t total_entries = 0;
    char* line = NULL;
    size_t buff_len = 0;
    ssize_t read_len = 0;

    #define buff_size 512 // this buffers save the tag data from previous line (#extinf) to be used on the next lie (url)
    char name[buff_size] = "";
    char logo[buff_size] = "";
    char group[buff_size] = "";
    char args[buff_size] = "";

    while ((read_len = getline(&line, &buff_len, fp)) != -1)
    {
        if (line[0] == '#')
        {
            // check if comment or tag
            if (sscanf(line, "#EXTINF:-1 %[^,],%[^\n\t]", args, name) != 2)
                continue; // comment or bad tag

            // parse tag arguments
            char* substr;

            if ((substr = strstr(args, "tvg-logo=\"")) != NULL )
                sscanf(substr + 10, "%[^\"]", logo);
            else
                strcpy(logo, "");

            if ((substr = strstr(args, "group-title=\"")) != NULL )
                sscanf(substr + 13, "%[^\"]", group);
            else
                strcpy(logo, group);

            // do something to treat this line maybe?
            //printf("\nName: %s\nLogo: %s\nGroup: %s\n", name, logo, group);
        }
        else
        {
            for (size_t i = 0;; i++) // remove new lines from text
                if (line[i] == '\r' || line[i] == '\r')
                    line[i] = '\0';
                else if (line[i] == '\0')
                    break;

            if (playlist_new_entry(playlist, group, name, logo, line) != NULL)
                total_entries++;
        }
    }

    return total_entries;
}

void playlist_print(playlist_t* playlist)
{
    for (uint8_t g = 0; g < playlist->num_groups; g++)
    {
        printf("\n\n%s:\n", playlist->groups[g].group_name);

        for (uint16_t e = 0; e < playlist->groups[g].num_entries; e++)
            printf("Name: %s\nLogo: %s\nUrl: %s\n", playlist->groups[g].entries[e].name, playlist->groups[g].entries[e].logo, playlist->groups[g].entries[e].url);
    }
}

// =====================================
// LOGO DOWNLOAD
// =====================================
void str_remove_special(const char* src, char* dst, size_t max_len)
{
    for (size_t i = 0; i < max_len; i++)
    {
        char c = src[i];

        if (c == '\0')
        {
            dst[i] = '\0';
            break;
        }
        else if (isdigit(c) || isalpha(c))
            dst[i] = c;
        else
            dst[i] = '-';
    }
}

GdkPixbuf* pixbuff_from_file(FILE* fp)
{
    if (fp == NULL) // sanity check
        return NULL;

    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (loader == NULL)
    {
        fprintf(stderr, "\nCould not create 'GdkPixbufLoader'\n");
        return NULL;
    }

    size_t read;
    uint8_t buffer[1024];

    while ((read = fread(buffer, sizeof(uint8_t), sizeof(buffer), fp )) > 0)
    {
        if (!gdk_pixbuf_loader_write(loader, buffer, read, NULL))
        {
            fprintf(stderr, "\ngdk_pixbuf_loader_write failed\n");
            return NULL;
        }
    }

    GdkPixbuf* pxb_1 = gdk_pixbuf_loader_get_pixbuf(loader);

    gdk_pixbuf_loader_close(loader, NULL);

    if (pxb_1 == NULL)
    {
        fprintf(stderr, "\ngdk_pixbuf_loader_get_pixbuf failed\n");
        return NULL;
    }

    GdkPixbuf* pxb_2 = gdk_pixbuf_scale_simple(pxb_1, 80, 80, GDK_INTERP_BILINEAR);

    g_object_unref(pxb_1);

    return pxb_2;
}

FILE* cache_or_download_file(const char* url)
{
    // make sure the cache directory exists - if not, then we create it
    static const char cache_dir[] = "cache";
    static const char user_agent[] = "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:81.0) Gecko/20100101 Firefox/81.0";

    struct stat st = {0};

    if (stat(cache_dir, &st) == -1)
    {
        fprintf(stderr, "\nDirectory '%s' does not exist: ", cache_dir);
        if (mkdir(cache_dir, 0777) == 0)
            fprintf(stderr, "created\n");
        else
        {
            fprintf(stderr, "%d %s", errno, strerror(errno));
            return NULL;
        }
    }

    // check if the file exists in the cache
    #define MAX_LEN_URL 100
    char file_name[MAX_LEN_URL];
    char parsed_url[MAX_LEN_URL - 6];

    str_remove_special(url, parsed_url, sizeof(parsed_url));
    snprintf(file_name, sizeof(file_name),"%s/%s", cache_dir, parsed_url);


    FILE* fp = NULL;

    if (access(file_name, F_OK) != -1)
    {
        // file is cached
        open_cached:
        if ((fp = fopen(file_name, "rb+")) == NULL)
        {
            fprintf(stderr, "\nFailed to open file '%s': %d %s\n", file_name, errno, strerror(errno));
            goto download; // failed to open
        }
        else
            return fp;
    }
    else
    {
        // file is not cached
        download: (void)0;

        CURL* curl = curl_easy_init();
        if (curl == NULL)
            return NULL; // cannot download

        if ((fp = fopen(file_name, "wb")) == NULL)
        {
            fprintf(stderr, "\nFailed to create file '%s': %d %s\n", file_name, errno, strerror(errno));
            curl_easy_cleanup(curl);
            return NULL; // could not create file
        }


        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        CURLcode result = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        fclose(fp); // close to save

        if (result)
        {
            // downlaod failed
            fprintf(stderr, "\nCurl failed to download '%s' to '%s': \n", url, file_name);
            remove(file_name);
            return NULL;
        }

        // download succeeded - file is now cached
        goto open_cached;
    }
}

GdkPixbuf* get_channel_logo(playlist_entry_t* chan)
{
    if (chan->private_data)
        return (GdkPixbuf*)chan->private_data;

    FILE* fp = cache_or_download_file(chan->logo);

    if (!fp)
        return NULL;

    GdkPixbuf* logo = pixbuff_from_file(fp);
    fclose(fp);

    chan->private_data = logo;

    return logo;
}

// =====================================
// GLOBAL
// =====================================
playlist_t playlist;

int selected_group = 0;
int selected_channel = 0;

GtkBuilder* builder;
GtkTreeView* chan_tree;
GtkTreeView* cat_tree;
GdkRectangle screen_rect;

libvlc_instance_t *vlc_inst;
libvlc_media_player_t* media_player;
GtkWidget* channel_player;
GtkWindow* main_window;

// =====================================
// GUI
// =====================================

void player_realize(GtkWidget* wid, gpointer data)
{
}

void destroy(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

void fill_groups_list()
{
    GtkTreeIter group_iter;
    static GtkTreeStore *cat_store;

    if (cat_store == NULL)
        cat_store = GTK_TREE_STORE(gtk_builder_get_object(builder, "cat_store"));

    for (uint8_t group_index = 0; group_index < playlist.num_groups; group_index++)
    {
        gtk_tree_store_append(cat_store, &group_iter, NULL);
        gtk_tree_store_set(cat_store, &group_iter, 0, playlist.groups[group_index].group_name, -1);
    }
}

void fill_channel_list()
{
    GtkTreeIter chan_iter;
    static GtkTreeStore *chan_store;

    if (chan_store == NULL)
        chan_store = GTK_TREE_STORE(gtk_builder_get_object(builder, "chan_store"));

    gtk_tree_store_clear(chan_store);

    playlist_group_t* gro = &playlist.groups[selected_group];

    for (uint8_t chan_index = 0; chan_index < gro->num_entries; chan_index++)
    {
        gtk_tree_store_append(chan_store, &chan_iter, NULL);
        gtk_tree_store_set(chan_store, &chan_iter, 0, get_channel_logo(&gro->entries[chan_index]), 1, gro->entries[chan_index].name, -1);
    }
}

int get_sel_index(GtkWidget *c)
{
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(GTK_TREE_SELECTION(c), &model, &iter))
        return 0;

    GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
    gint* i = gtk_tree_path_get_indices(path);

    return (int)i[0];
}

void cat_sel_change(GtkWidget *c)
{
    selected_group = get_sel_index(c);
    fill_channel_list();
}

void chan_sel_change(GtkWidget *c)
{
    selected_channel = get_sel_index(c);
    printf("channel = %s\n", playlist.groups[selected_group].entries[selected_channel].name);
}

void player_url(const char* url, GtkWidget* wid)
{
    if (url == NULL ||  wid == NULL)
        return;

    if (libvlc_media_player_is_playing(media_player))
        libvlc_media_player_stop(media_player);

    if (!gtk_widget_is_visible(wid))
        gtk_widget_show(wid);

    libvlc_media_player_set_xwindow(media_player, GDK_WINDOW_XID(gtk_widget_get_window(wid)));

    libvlc_media_t *media = libvlc_media_new_location(vlc_inst, url);
    libvlc_media_player_set_media(media_player, media);

    libvlc_media_player_play(media_player);
    libvlc_media_release(media);
}


void player_do(uint8_t bOpen)
{
    if (bOpen)
    {
        static char* last_url;
        char* url = playlist.groups[selected_group].entries[selected_channel].url;

        printf("\nPlay URL = '%s'\n", url);

        if (libvlc_media_player_is_playing(media_player) && last_url == url) // repeate play command
        {
            gtk_widget_hide(channel_player);
            player_url(url, GTK_WIDGET(main_window));
        }
        else
        {
            last_url = url;
            player_url(url, channel_player);
        }
    }
    else
    {
        libvlc_media_player_stop(media_player);
        libvlc_media_player_set_media(media_player, NULL);
        libvlc_media_player_set_xwindow(media_player, 0);
        gtk_widget_hide(GTK_WIDGET(channel_player));
    }
}

gboolean chan_tree_key(GtkWidget* widget, GdkEventKey *event, gpointer data)
{
    switch (event->keyval)
    {
        case GDK_KEY_Left:
            gtk_widget_grab_focus(GTK_WIDGET(cat_tree));
        break;

        case GDK_KEY_Return:
            player_do(1);
        break;

        case GDK_KEY_Escape:
        case GDK_KEY_Home:
        case GDK_KEY_BackSpace:
            player_do(0);
        break;
    }

    return FALSE;
}

gboolean cat_tree_key(GtkWidget* widget, GdkEventKey *event, gpointer data)
{
    switch (event->keyval)
    {
        case GDK_KEY_Right:
        case GDK_KEY_Return:
            gtk_widget_grab_focus(GTK_WIDGET(chan_tree));
        break;

        case GDK_KEY_Escape:
        case GDK_KEY_Home:
        case GDK_KEY_BackSpace:
            player_do(0);
        break;
    }

    return FALSE;
}

// =====================================
// MAIN
// =====================================
int main(int argc, char** argv)
{
    // sanity check
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s filename.m3u", argv[0]);
        return EINVAL;
    }

    // load playlist
    if ( (read_playlist(argv[1], &playlist)) == 0)
        return errno;

    //playlist_print(&playlist);

    // Create GUI
    // ==================================================

    // setup vlc
    const char* const vlc_params[] =
    #ifdef _PLAYER_USE_XLIB_
       {""};
    XInitThreads();
    #else
        {"--no-xlib"};
    #endif

    vlc_inst = libvlc_new(1, vlc_params);
    media_player = libvlc_media_player_new(vlc_inst);

    // main window
    gtk_init (&argc, &argv);

    GtkCssProvider *css = gtk_css_provider_new();
    if (css == NULL)
    {
        fprintf(stderr, "\ngtk_css_provider_new failed\n");
        return -1;
    }

    gtk_css_provider_load_from_path(css, "theme.css", NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);

    builder = gtk_builder_new_from_file("layout.glade");
    if (builder == NULL)
    {
        fprintf(stderr, "\ngtk_builder_new_from_file failed\n");
        return -1;
    }

    gtk_builder_connect_signals(builder, NULL);

    main_window = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
    if (main_window == NULL)
        return -1;

    gdk_monitor_get_workarea(gdk_display_get_primary_monitor(gdk_display_get_default()), &screen_rect);
    gtk_window_set_default_size(main_window, screen_rect.width, screen_rect.height);
    gtk_window_fullscreen(main_window);

    g_signal_connect(G_OBJECT(main_window), "destroy", G_CALLBACK(destroy), NULL);

    chan_tree = GTK_TREE_VIEW(gtk_builder_get_object(builder, "chan_tree"));
    cat_tree = GTK_TREE_VIEW(gtk_builder_get_object(builder, "cat_tree"));
    channel_player = GTK_WIDGET(gtk_builder_get_object(builder, "player_area"));

    // channels list
    fill_groups_list();

    // main GTK
    gtk_widget_show_all(GTK_WIDGET(main_window));
    gtk_widget_grab_focus(GTK_WIDGET(cat_tree));
    gtk_main ();

    // cleanup
    libvlc_media_player_release(media_player);
    libvlc_release(vlc_inst);
    playlist_destroy(&playlist);
    return 0;
}

