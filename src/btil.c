#include <stdio.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <curl/curl.h>
#include <jansson.h>

struct School {
	const char* id;
	const char* name;
	const char* address;
	const char* wellKnown;
};

struct Response {
	char* buf;
	size_t size;
};

static GtkTreeView* sclList;
static GtkListStore* schoolsModel;

static struct School lastSearch[100];
static int lastSearchLength = 0;

static json_t* wellKnown;

static size_t curl_cb(char* data, size_t size, size_t nmemb, void* userdata) {
	size_t realsize = size * nmemb;
	struct Response* mem = (struct Response *)userdata;

	char* ptr = realloc(mem->buf, mem->size + realsize + 1);
	if (ptr == NULL) {
		return 0;
	}

	mem->buf = ptr;
	memcpy(&(mem->buf[mem->size]), data, realsize);
	mem->size += realsize;
	mem->buf[mem->size] = 0;

	return realsize;
}

/**
 * @param text Text search parameter
 * @param schools Output array of return schools, max 100
 * 
 * @returns count of schools written to schools
 */
static int searchSchools(const char* text, struct School* schools) {
	CURL* curl = curl_easy_init();
	if (!curl) abort();

	char* endpoint = "https://api.skolengo.com/api/v1/bff-sko-app/schools?filter%5Btext%5D=";
	char url[strlen(endpoint) + strlen(text) + 1];
	sprintf(url, "%s%s", endpoint, text);
	curl_easy_setopt(curl, CURLOPT_URL, url);

	struct Response res = {0};
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&res);

	CURLcode curlcode = curl_easy_perform(curl);
	if (curlcode != CURLE_OK) {
		fprintf(stderr, "Error while searching schools for \"%s\": %s", text, curl_easy_strerror(curlcode));
		fflush(stdout);
		abort();
	}
	long status;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

	if (status != 200) {
		fprintf(stderr, "Got code %i\n", status);
		return 0;
	}

	curl_easy_cleanup(curl);

	json_error_t error;
	json_t* root = json_loadb(res.buf, res.size, 0, &error);
	free(res.buf);

	json_t* data = json_object_get(root, "data");
	int i = 0;
	for (; i < json_array_size(data) && i < 100; i++) {
		json_t* jscl = json_array_get(data, i);
		json_t* attr = json_object_get(jscl, "attributes");
		struct School scl;
		scl.id = json_string_value(json_object_get(jscl, "id"));
		scl.name = json_string_value(json_object_get(attr, "name"));
		scl.wellKnown = json_string_value(json_object_get(attr, "emsOIDCWellKnownUrl"));
		json_t* al1 = json_object_get(attr, "addressLine1");
		json_t* al2 = json_object_get(attr, "addressLine2");
		json_t* al3 = json_object_get(attr, "addressLine3");
		json_t* zip = json_object_get(attr, "zipCode");
		json_t* city = json_object_get(attr, "city");
		json_t* country = json_object_get(attr, "country");
		char* address = (char*) malloc(256); // XXX memleak here
		snprintf(address, 255, "%s %s %s %s %s %s",
				json_is_null(al1) ? "" : json_string_value(al1),
				json_is_null(al2) ? "" : json_string_value(al2),
				json_is_null(al3) ? "" : json_string_value(al3),
				json_is_null(zip) ? "" : json_string_value(zip),
				json_is_null(city) ? "" : json_string_value(city),
				json_is_null(country) ? "" : json_string_value(country)
				);
		scl.address = address;

		schools[i] = scl;
	}

	return i;
}

static void on_school_search(GtkWidget* widget, gpointer userdata) {
	GtkSearchEntry* txtSearch = GTK_SEARCH_ENTRY(userdata);

	gtk_list_store_clear(schoolsModel);
	int lastSearchLength = searchSchools(gtk_entry_get_text(GTK_ENTRY(txtSearch)), lastSearch);

	fprintf(stderr, "Got %i schools\n", lastSearchLength);

	for (int i = 0; i < lastSearchLength; i++) {
		struct School s = lastSearch[i];
		fprintf(stderr, "%s %s %s %s\n", s.id, s.name, s.address, s.wellKnown);
		GtkTreeIter iter;
		gtk_list_store_append(schoolsModel, &iter);
		gtk_list_store_set(schoolsModel, &iter, 0, s.name, 1, s.address, 2, i, -1);
	}
}

/**
 * @returns malloc'd URL to browse to and let the user consent
 */
static void fetch_well_known(const char* wellKnownUrl) {
	CURL* curl = curl_easy_init();
	if (!curl) abort();

	curl_easy_setopt(curl, CURLOPT_URL, wellKnownUrl);
	
	struct Response res = {0};
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&res);

	CURLcode curlcode = curl_easy_perform(curl);
	if (curlcode != CURLE_OK) {
		fprintf(stderr, "Error while fetching consent URL: %s", curl_easy_strerror(curlcode));
		abort();
	}

	long status;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

	if (status != 200) {
		fprintf(stderr, "Got code %i\n", status);
		abort();
	}

	curl_easy_cleanup(curl);

	json_error_t error;
	wellKnown = json_loadb(res.buf, res.size, 0, &error);
	free(res.buf);
}

static const char* get_consent_url() {
	const char* authzEndpoint = json_string_value(json_object_get(wellKnown, "authorization_endpoint"));

	char* endpointWithParams = (char*) malloc(1024);

	snprintf(endpointWithParams, 1024, "%s%s", authzEndpoint, "?response_type=code&scope=openid&client_id=SkoApp.Prod.0d349217-9a4e-41ec-9af9-df9e69e09494&redirect_uri=skoapp-prod://sign-in-callback");

	return endpointWithParams;
}

static json_t* code_request(const char* code) {
	CURL* curl = curl_easy_init();
	if (!curl) abort();

	curl_easy_setopt(curl, CURLOPT_URL, json_string_value(json_object_get(wellKnown, "token_endpoint")));

	char data[1024];
	snprintf(data, 1024, "grant_type=authorization_code&client_id=SkoApp.Prod.0d349217-9a4e-41ec-9af9-df9e69e09494&client_secret=7cb4d9a8-2580-4041-9ae8-d5803869183f&redirect_uri=skoapp-prod://sign-in-callback&code=%s", code);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(curl, CURLOPT_POST, 1);

	struct Response res = {0};
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&res);

	CURLcode curlcode = curl_easy_perform(curl);
	if (curlcode != CURLE_OK) {
		fprintf(stderr, "Error while getting tokenset: %s\n", curl_easy_strerror(curlcode));
		abort();
	}

	long status;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

	if (status != 200) {
		fprintf(stderr, "Got code %i\n", status);
		abort();
	}

	curl_easy_cleanup(curl);

	json_error_t error;
	json_t* tokenset = json_loadb(res.buf, res.size, 0, &error);
	free(res.buf);

	return tokenset;
}

static void on_web_load(WebKitWebView* webView, WebKitLoadEvent event, gpointer userData) {
	const char* uri = webkit_web_view_get_uri(webView);

	if (strncmp("skoapp-prod://sign-in-callback?code=", uri, 36) == 0 && event == WEBKIT_LOAD_REDIRECTED) {
		fprintf(stderr, "Logged in!\n");

		char code[1024];
		strcpy(code, uri + 36);

		json_t* tokenset = code_request(code);
		const char* json = json_dumps(tokenset, JSON_INDENT(4));
		fprintf(stdout, "%s\n", json);
		fflush(stdout);
	}
}

static void on_school_select(void) {
	GtkTreeSelection* sel = gtk_tree_view_get_selection(sclList);
	GtkTreeIter iter;
	GtkTreeModel* m = GTK_TREE_MODEL(schoolsModel);

	if (!gtk_tree_selection_get_selected(sel, &m, &iter)) {
		return;
	}

	int idx;
	gtk_tree_model_get(m, &iter, 2, &idx, -1);
	struct School s = lastSearch[idx];
	fprintf(stderr, "Selected %i %s %s %s %s\n", idx, s.id, s.name, s.address, s.wellKnown);

	fetch_well_known(s.wellKnown);
	const char* consentUrl = get_consent_url();

	GError* error = NULL;
	GtkBuilder* builder = gtk_builder_new();
	if (gtk_builder_add_from_file(builder, "Consent.glade", &error) == 0) {
		fprintf(stderr, "Error building consent UI: %s\n", error->message);
		abort();
	}

	GtkWindow* consentWindow = GTK_WINDOW(gtk_builder_get_object(builder, "consentWindow"));
	gtk_widget_show_all(GTK_WIDGET(consentWindow));
	WebKitWebView* webView = WEBKIT_WEB_VIEW(gtk_builder_get_object(builder, "webView"));

	webkit_web_view_load_uri(webView, consentUrl);
	g_signal_connect(G_OBJECT(webView), "load-changed", G_CALLBACK(on_web_load), NULL);
}

static void app_activate(GtkApplication* app, gpointer user_data) {
	GError* error = NULL;
	GtkBuilder* builder = gtk_builder_new();
	if (gtk_builder_add_from_file(builder, "Wizard.glade", &error) == 0) {
		fprintf(stderr, "Error building UI: %s\n", error->message);
		abort();
	}

	GtkDialog* window = GTK_DIALOG(gtk_builder_get_object(builder, "dialogSelectSchool"));
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(GTK_WIDGET(window));
	gtk_window_present(GTK_WINDOW(window));

	sclList = GTK_TREE_VIEW(gtk_builder_get_object(builder, "schoolList"));
	GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(sclList, -1, "Nom", renderer, "text", 0, NULL);
	gtk_tree_view_insert_column_with_attributes(sclList, -1, "Adresse", renderer, "text", 1, NULL);
	gtk_tree_view_set_model(sclList, GTK_TREE_MODEL(schoolsModel));

	GtkSearchEntry* txtSearch = GTK_SEARCH_ENTRY(gtk_builder_get_object(builder, "txtSearch"));

	GtkButton* btnSelect = GTK_BUTTON(gtk_builder_get_object(builder, "btnSelect"));

	g_signal_connect(G_OBJECT(txtSearch), "search-changed", G_CALLBACK(on_school_search), txtSearch);
	g_signal_connect(G_OBJECT(btnSelect), "clicked", G_CALLBACK(on_school_select), btnSelect);
}

int main(void) {
	schoolsModel = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

	gtk_init(0, NULL);
	GtkApplication* app = gtk_application_new("me.vinceh121.buttauriislighter", G_APPLICATION_DEFAULT_FLAGS);
	g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);

	int status = g_application_run(G_APPLICATION(app), 0, NULL);
	g_object_unref(app);

	gtk_main();

	return status;
}

