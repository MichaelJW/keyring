
#ifdef __linux__

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include <libsecret/secret.h>

void R_init_keyring(DllInfo *info) {
  g_type_ensure (G_TYPE_OBJECT);
}

void R_unload_keyring(DllInfo *info) {
  secret_service_disconnect();
 }

const SecretSchema *keyring_secret_service_schema() {
  static const SecretSchema schema = {
    "com.rstudio.keyring.password", SECRET_SCHEMA_NONE, {
      {  "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
      {  "username", SECRET_SCHEMA_ATTRIBUTE_STRING },
      {  NULL, 0 },
    }
  };

  return &schema;
}

void keyring_secret_service_handle_status(const char *func, gboolean status,
					  GError *err) {
  if (!status || err) {
    g_error_free (err);
    error("Secret service keyring error in '%s': '%s'", func, "TODO");
  }
}

SEXP keyring_secret_service_get(SEXP service, SEXP username) {

  const char* empty = "";
  const char* cservice = CHAR(STRING_ELT(service, 0));
  const char* cusername =
    isNull(username) ? empty : CHAR(STRING_ELT(username, 0));

  GError *err = NULL;

  gchar *password = secret_password_lookup_sync (
    keyring_secret_service_schema(),
    /* cancellable = */ NULL,
    &err,
    "service", cservice,
    "username", cusername,
    NULL);

  if (err) {
    if (password) secret_password_free(password);
    keyring_secret_service_handle_status("get", TRUE, err);
  }

  if (!password) {
    error("keyring item not found");
    return R_NilValue;

  } else {
    SEXP result = PROTECT(ScalarString(mkChar(password)));
    if (password) secret_password_free(password);
    UNPROTECT(1);
    return result;
  }
}

SEXP keyring_secret_service_set(SEXP service, SEXP username,
				SEXP password) {
  const char* empty = "";
  const char* cservice = CHAR(STRING_ELT(service, 0));
  const char* cusername =
    isNull(username) ? empty : CHAR(STRING_ELT(username, 0));
  const char* cpassword = CHAR(STRING_ELT(password, 0));

  GError *err = NULL;

  gboolean status = secret_password_store_sync(
    keyring_secret_service_schema(),
    SECRET_COLLECTION_DEFAULT,
    /* label = */ "TODO",
    cpassword,
    /* cancellable = */ NULL,
    &err,
    "service", cservice,
    "username", cusername,
    NULL);

  keyring_secret_service_handle_status("set", status, err);

  return R_NilValue;
}

SEXP keyring_secret_service_delete(SEXP service, SEXP username) {

  const char* empty = "";
  const char* cservice = CHAR(STRING_ELT(service, 0));
  const char* cusername =
    isNull(username) ? empty : CHAR(STRING_ELT(username, 0));

  GError *err = NULL;

  gboolean status = secret_password_clear_sync(
    keyring_secret_service_schema(),
    /* cancellable = */ NULL,
    &err,
    "service", cservice,
    "username", cusername,
    NULL);

  keyring_secret_service_handle_status("get", TRUE, err);

  return R_NilValue;
}

SEXP keyring_secret_service_list(SEXP service) {

  const char *cservice = isNull(service) ? NULL : CHAR(STRING_ELT(service, 0));
  const char *keyring = "default";

  const char *errormsg = NULL;

  SecretCollection *collection = NULL;
  GList *secretlist = NULL, *iter = NULL;
  guint listlength, i;
  GHashTable *attributes = NULL;
  GError *err = NULL;

  SEXP result = R_NilValue;

  /* Need to set the LOAD_COLLECTIONS flag, otherwise we cannot access the
     collections. */

  SecretService *secretservice = secret_service_get_sync(
    /* flags = */ SECRET_SERVICE_LOAD_COLLECTIONS,
    /* cancellable = */ NULL,
    &err);

  if (err || !secretservice) {
    errormsg = "Cannot connect to secret service";
    goto cleanup;
  }

  collection = secret_collection_for_alias_sync(
    /* service = */ secretservice,
    /* alias = */ keyring,
    /* flags = */ SECRET_COLLECTION_NONE,
    /* cancellable = */ NULL,
    &err);

  if (err || !collection) {
    errormsg = "Cannot find keyring";
    goto cleanup;
  }

  /* If service is not NULL, then we only look for the specified service. */
  attributes = g_hash_table_new(
    /* hash_func = */ (GHashFunc) g_str_hash,
    /* key_equal_func = */ (GEqualFunc) g_str_equal);

  if (cservice) {
    g_hash_table_insert(attributes, g_strdup("service"), g_strdup(cservice));
  }

  secretlist = secret_collection_search_sync(
    /* self = */ collection,
    /* schema = */ keyring_secret_service_schema(),
    /* attributes = */ attributes,
    /* flags = */ SECRET_SEARCH_ALL,
    /* cancellable = */ NULL,
    &err);

  if (err) goto cleanup;

  listlength = g_list_length(secretlist);
  result = PROTECT(allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, allocVector(STRSXP, listlength));
  SET_VECTOR_ELT(result, 1, allocVector(STRSXP, listlength));
  for (i = 0, iter = g_list_first(secretlist); iter; i++, iter = g_list_next(iter)) {
    SecretItem *secret = iter->data;
    GHashTable *attr = secret_item_get_attributes(secret);
    char *service = g_hash_table_lookup(attr, "service");
    char *username = g_hash_table_lookup(attr, "username");
    SET_STRING_ELT(VECTOR_ELT(result, 0), i, mkChar(service));
    SET_STRING_ELT(VECTOR_ELT(result, 1), i, mkChar(username));
  }

  UNPROTECT(1);

  /* If an error happened, then err is not NULL, and the handler longjumps.
     Otherwise if errormsg is not NULL, then we error out with that. This
     happens for example if the specified keyring is not found. */

 cleanup:
  if (secretservice) g_object_unref(secretservice);
  if (collection) g_object_unref(collection);
  if (secretlist) g_list_free(secretlist);
  if (attributes) g_hash_table_unref(attributes);
  keyring_secret_service_handle_status("list", TRUE, err);
  if (errormsg) error(errormsg);

  return result;
}

#endif // __linux__
