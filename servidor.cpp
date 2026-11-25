// Grupo 7
// Nomes: Leonardo Campos, Matheus Henrique, Wisley Victor, Lucas Rodrigues
// SERVIDOR (transmissor) com interface GTK 3
// Pipeline (a mesma do transmissor.cpp):
//   fonte -> audioconvert -> audioresample -> opusenc -> rtpopuspay -> udpsink

#include <gtk/gtk.h>
#include <gst/gst.h>

struct App
{
    GtkWidget *ip, *porta, *fonte, *bitrate, *quadro; // campos
    GtkWidget *estado, *btn_iniciar, *btn_parar;
    GstElement *pipeline = nullptr;
    guint bus_watch = 0;
};

static void mostrar_estado(App *a, const char *texto)
{
    gtk_label_set_text(GTK_LABEL(a->estado), texto);
}

// Interrompe a pipeline e libera os recursos
static void parar(App *a)
{
    if (a->bus_watch)
    {
        g_source_remove(a->bus_watch);
        a->bus_watch = 0;
    }
    if (a->pipeline)
    {
        gst_element_set_state(a->pipeline, GST_STATE_NULL);
        gst_object_unref(a->pipeline);
        a->pipeline = nullptr;
    }
    gtk_widget_set_sensitive(a->btn_iniciar, TRUE);
    gtk_widget_set_sensitive(a->btn_parar, FALSE);
}

// Mensagens do bus, tratadas no loop principal (sem bloquear a interface)
static gboolean ao_mensagem(GstBus *, GstMessage *msg, gpointer dados)
{
    App *a = (App *)dados;
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
    {
        GError *err = nullptr;
        gst_message_parse_error(msg, &err, nullptr);
        gchar *txt = g_strdup_printf("Erro em %s: %s", GST_OBJECT_NAME(msg->src), err->message);
        g_error_free(err);
        a->bus_watch = 0; // o watch acaba ao retornar FALSE
        parar(a);
        mostrar_estado(a, txt);
        g_free(txt);
        return FALSE;
    }
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_STATE_CHANGED &&
        GST_MESSAGE_SRC(msg) == GST_OBJECT(a->pipeline))
    {
        GstState novo;
        gst_message_parse_state_changed(msg, nullptr, &novo, nullptr);
        if (novo == GST_STATE_PLAYING)
        {
            gchar *txt = g_strdup_printf("Transmitindo para %s:%d",
                                         gtk_entry_get_text(GTK_ENTRY(a->ip)),
                                         gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->porta)));
            mostrar_estado(a, txt);
            g_free(txt);
        }
    }
    return TRUE;
}

static void iniciar_transmissao(GtkButton *, gpointer dados)
{
    App *a = (App *)dados;

    // 1. Recuperar e validar os valores da interface
    const char *ip = gtk_entry_get_text(GTK_ENTRY(a->ip));
    int porta = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->porta));
    int bitrate = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->bitrate));
    bool mic = gtk_combo_box_get_active(GTK_COMBO_BOX(a->fonte)) == 1;
    gchar *quadro = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(a->quadro));

    if (!g_hostname_is_ip_address(ip))
    {
        mostrar_estado(a, "IP de destino invalido");
        g_free(quadro);
        return;
    }

    // 2. Criar os elementos (igual ao transmissor.cpp)
    a->pipeline = gst_pipeline_new("transmissor");
    GstElement *fonte = gst_element_factory_make(mic ? "autoaudiosrc" : "audiotestsrc", "fonte");
    GstElement *conv = gst_element_factory_make("audioconvert", "conv");
    GstElement *resamp = gst_element_factory_make("audioresample", "resamp");
    GstElement *enc = gst_element_factory_make("opusenc", "enc");
    GstElement *pay = gst_element_factory_make("rtpopuspay", "pay");
    GstElement *sink = gst_element_factory_make("udpsink", "sink");

    if (!fonte || !conv || !resamp || !enc || !pay || !sink)
    {
        mostrar_estado(a, "Elemento nao encontrado (verifique os plugins)");
        g_free(quadro);
        parar(a);
        return;
    }

    // 3. Configurar com os valores da interface
    if (!mic)
        g_object_set(fonte, "is-live", TRUE, "freq", 440.0, nullptr);
    g_object_set(enc, "bitrate", bitrate * 1000, nullptr);        // qualidade 1: bitrate
    gst_util_set_object_arg(G_OBJECT(enc), "frame-size", quadro); // qualidade 2: ms por pacote
    g_object_set(pay, "pt", 96, nullptr);
    g_object_set(sink, "host", ip, "port", porta, nullptr);
    g_free(quadro);

    gst_bin_add_many(GST_BIN(a->pipeline), fonte, conv, resamp, enc, pay, sink, nullptr);
    if (!gst_element_link_many(fonte, conv, resamp, enc, pay, sink, nullptr))
    {
        mostrar_estado(a, "Falha ao ligar os elementos");
        parar(a);
        return;
    }

    // 4. Bus + PLAYING
    GstBus *bus = gst_element_get_bus(a->pipeline);
    a->bus_watch = gst_bus_add_watch(bus, ao_mensagem, a);
    gst_object_unref(bus);

    if (gst_element_set_state(a->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        parar(a);
        mostrar_estado(a, "Nao foi possivel iniciar a transmissao");
        return;
    }
    mostrar_estado(a, "Iniciando...");
    gtk_widget_set_sensitive(a->btn_iniciar, FALSE);
    gtk_widget_set_sensitive(a->btn_parar, TRUE);
}

static void interromper_transmissao(GtkButton *, gpointer dados)
{
    App *a = (App *)dados;
    parar(a);
    mostrar_estado(a, "Parado");
}

static void ao_fechar(GtkWidget *, gpointer dados)
{
    parar((App *)dados);
    gtk_main_quit();
}

// Linha da grade: rotulo + campo
static void linha(GtkWidget *grade, int n, const char *rotulo, GtkWidget *campo)
{
    GtkWidget *l = gtk_label_new(rotulo);
    gtk_widget_set_halign(l, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grade), l, 0, n, 1, 1);
    gtk_grid_attach(GTK_GRID(grade), campo, 1, n, 1, 1);
}

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);
    gtk_init(&argc, &argv);
    static App a;

    GtkWidget *janela = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(janela), "Servidor Multimidia");
    gtk_window_set_default_size(GTK_WINDOW(janela), 380, 260);
    gtk_container_set_border_width(GTK_CONTAINER(janela), 12);
    g_signal_connect(janela, "destroy", G_CALLBACK(ao_fechar), &a);

    // Campos
    a.ip = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(a.ip), "127.0.0.1");
    a.porta = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a.porta), 5000);

    a.fonte = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.fonte), "Tom de 440 Hz");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.fonte), "Microfone");
    gtk_combo_box_set_active(GTK_COMBO_BOX(a.fonte), 0);

    a.bitrate = gtk_spin_button_new_with_range(16, 256, 8);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a.bitrate), 64);

    a.quadro = gtk_combo_box_text_new();
    const char *tamanhos[] = {"10", "20", "40", "60"};
    for (const char *t : tamanhos)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.quadro), t);
    gtk_combo_box_set_active(GTK_COMBO_BOX(a.quadro), 1); // 20 ms

    // Botoes e estado
    a.btn_iniciar = gtk_button_new_with_label("Iniciar");
    a.btn_parar = gtk_button_new_with_label("Interromper");
    gtk_widget_set_sensitive(a.btn_parar, FALSE);
    g_signal_connect(a.btn_iniciar, "clicked", G_CALLBACK(iniciar_transmissao), &a);
    g_signal_connect(a.btn_parar, "clicked", G_CALLBACK(interromper_transmissao), &a);
    a.estado = gtk_label_new("Parado");

    // Layout
    GtkWidget *grade = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grade), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grade), 8);
    linha(grade, 0, "IP de destino:", a.ip);
    linha(grade, 1, "Porta UDP:", a.porta);
    linha(grade, 2, "Fonte:", a.fonte);
    linha(grade, 3, "Bitrate (kbps):", a.bitrate);
    linha(grade, 4, "Quadro (ms):", a.quadro);

    GtkWidget *botoes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(botoes), a.btn_iniciar, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), a.btn_parar, TRUE, TRUE, 0);

    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(caixa), grade, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), botoes, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), a.estado, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(janela), caixa);

    gtk_widget_show_all(janela);
    gtk_main();
    return 0;
}