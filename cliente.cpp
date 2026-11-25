// Grupo 7
// Nomes: Leonardo Campos, Matheus Henrique, Wisley Victor, Lucas Rodrigues
// CLIENTE (receptor) com interface GTK 3
// Pipeline (a mesma do receptor.cpp):
//   udpsrc -> rtpjitterbuffer -> rtpopusdepay -> opusdec -> audioconvert -> audioresample -> autoaudiosink

#include <gtk/gtk.h>
#include <gst/gst.h>

struct App
{
    GtkWidget *porta, *latencia; // campos
    GtkWidget *estado, *placar, *btn_iniciar, *btn_parar;
    GstElement *pipeline = nullptr, *jb = nullptr;
    guint bus_watch = 0, timer = 0;
    bool tocando = false;
};

static void mostrar_estado(App *a, const char *texto)
{
    gtk_label_set_text(GTK_LABEL(a->estado), texto);
}

// Interrompe a pipeline e libera os recursos
static void parar(App *a)
{
    if (a->timer)
    {
        g_source_remove(a->timer);
        a->timer = 0;
    }
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
        a->jb = nullptr;
    }
    a->tocando = false;
    gtk_widget_set_sensitive(a->btn_iniciar, TRUE);
    gtk_widget_set_sensitive(a->btn_parar, FALSE);
}

// Placar a cada 1 s (estatisticas do rtpjitterbuffer, como no receptor.cpp).
// PLAYING nao garante que chegou audio: o estado so vira "Recebendo" quando
// o jitterbuffer realmente entrega pacotes.
static gboolean placar(gpointer dados)
{
    App *a = (App *)dados;
    GstStructure *st = nullptr;
    guint64 recebidos = 0, perdidos = 0, atrasados = 0;
    g_object_get(a->jb, "stats", &st, nullptr);
    if (st)
    {
        gst_structure_get(st, "num-pushed", G_TYPE_UINT64, &recebidos,
                          "num-lost", G_TYPE_UINT64, &perdidos,
                          "num-late", G_TYPE_UINT64, &atrasados, nullptr);
        gst_structure_free(st);
    }
    gchar *txt = g_strdup_printf("Recebidos: %llu   Perdidos: %llu   Atrasados: %llu",
                                 (unsigned long long)recebidos, (unsigned long long)perdidos,
                                 (unsigned long long)atrasados);
    gtk_label_set_text(GTK_LABEL(a->placar), txt);
    g_free(txt);

    if (recebidos > 0 && !a->tocando)
    {
        a->tocando = true;
        mostrar_estado(a, "Recebendo audio");
    }
    return G_SOURCE_CONTINUE;
}

// Mensagens do bus, tratadas no loop principal
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
        if (novo == GST_STATE_PLAYING && !a->tocando)
            mostrar_estado(a, "Aguardando pacotes...");
    }
    return TRUE;
}

static void iniciar_recepcao(GtkButton *, gpointer dados)
{
    App *a = (App *)dados;

    // 1. Recuperar os valores da interface
    int porta = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->porta));
    int latencia = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->latencia));

    // 2. Criar os elementos (igual ao receptor.cpp)
    a->pipeline = gst_pipeline_new("receptor");
    GstElement *src = gst_element_factory_make("udpsrc", "src");
    a->jb = gst_element_factory_make("rtpjitterbuffer", "jb");
    GstElement *depay = gst_element_factory_make("rtpopusdepay", "depay");
    GstElement *dec = gst_element_factory_make("opusdec", "dec");
    GstElement *conv = gst_element_factory_make("audioconvert", "conv");
    GstElement *res = gst_element_factory_make("audioresample", "res");
    GstElement *sink = gst_element_factory_make("autoaudiosink", "sink");

    if (!src || !a->jb || !depay || !dec || !conv || !res || !sink)
    {
        mostrar_estado(a, "Elemento nao encontrado (verifique os plugins)");
        parar(a);
        return;
    }

    // 3. Configurar
    GstCaps *caps = gst_caps_from_string(
        "application/x-rtp,media=audio,encoding-name=OPUS,clock-rate=48000,payload=96");
    g_object_set(src, "address", "0.0.0.0", "port", porta, "caps", caps, "reuse", FALSE, nullptr);
    gst_caps_unref(caps);
    g_object_set(a->jb, "latency", latencia, "do-lost", TRUE, nullptr);
    g_object_set(dec, "plc", TRUE, nullptr);

    gst_bin_add_many(GST_BIN(a->pipeline), src, a->jb, depay, dec, conv, res, sink, nullptr);
    if (!gst_element_link_many(src, a->jb, depay, dec, conv, res, sink, nullptr))
    {
        mostrar_estado(a, "Falha ao ligar os elementos");
        parar(a);
        return;
    }

    // 4. Bus + placar + PLAYING
    GstBus *bus = gst_element_get_bus(a->pipeline);
    a->bus_watch = gst_bus_add_watch(bus, ao_mensagem, a);
    gst_object_unref(bus);

    if (gst_element_set_state(a->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        parar(a);
        mostrar_estado(a, "Nao foi possivel iniciar (porta em uso?)");
        return;
    }
    a->timer = g_timeout_add_seconds(1, placar, a);
    mostrar_estado(a, "Iniciando...");
    gtk_widget_set_sensitive(a->btn_iniciar, FALSE);
    gtk_widget_set_sensitive(a->btn_parar, TRUE);
}

static void interromper_recepcao(GtkButton *, gpointer dados)
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
    gtk_window_set_title(GTK_WINDOW(janela), "Cliente Multimidia");
    gtk_window_set_default_size(GTK_WINDOW(janela), 420, 220);
    gtk_container_set_border_width(GTK_CONTAINER(janela), 12);
    g_signal_connect(janela, "destroy", G_CALLBACK(ao_fechar), &a);

    // Campos
    a.porta = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a.porta), 5000);
    a.latencia = gtk_spin_button_new_with_range(10, 2000, 10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a.latencia), 200);

    // Botoes, estado e placar
    a.btn_iniciar = gtk_button_new_with_label("Iniciar");
    a.btn_parar = gtk_button_new_with_label("Interromper");
    gtk_widget_set_sensitive(a.btn_parar, FALSE);
    g_signal_connect(a.btn_iniciar, "clicked", G_CALLBACK(iniciar_recepcao), &a);
    g_signal_connect(a.btn_parar, "clicked", G_CALLBACK(interromper_recepcao), &a);
    a.estado = gtk_label_new("Parado");
    a.placar = gtk_label_new("Recebidos: 0   Perdidos: 0   Atrasados: 0");

    // Layout
    GtkWidget *grade = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grade), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grade), 8);
    linha(grade, 0, "Porta UDP:", a.porta);
    linha(grade, 1, "Latencia (ms):", a.latencia);

    GtkWidget *botoes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(botoes), a.btn_iniciar, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(botoes), a.btn_parar, TRUE, TRUE, 0);

    GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(caixa), grade, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), botoes, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), a.estado, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(caixa), a.placar, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(janela), caixa);

    gtk_widget_show_all(janela);
    gtk_main();
    return 0;
}