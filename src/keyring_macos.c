
#ifdef __APPLE__

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <R.h>
#include <Rinternals.h>

#include <sys/param.h>
#include <string.h>

void keyring_macos_error(const char *func, OSStatus status) {
  CFStringRef str = SecCopyErrorMessageString(status, NULL);
  CFIndex length = CFStringGetLength(str);
  CFIndex maxSize =
    CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  char *buffer = R_alloc(1, maxSize);

  if (CFStringGetCString(str, buffer, maxSize, kCFStringEncodingUTF8)) {
    error("macOS Keychain error in '%s': %s", func, buffer);

  } else {
    error("macOS Keychain error in '%s': %s", func, "unknown error");
  }
}

void keyring_macos_handle_status(const char *func, OSStatus status) {
  if (status != errSecSuccess) keyring_macos_error(func, status);
}

SecKeychainRef keyring_macos_open_keychain(const char *pathName) {
  SecKeychainRef keychain;
  OSStatus status = SecKeychainOpen(pathName, &keychain);
  if (status != errSecSuccess) keyring_macos_error("open", status);
  return keychain;
}

/* TODO: set encoding to UTF-8? */

SEXP keyring_macos_get(SEXP keyring, SEXP service, SEXP username) {

  const char* empty = "";
  const char* cservice = CHAR(STRING_ELT(service, 0));
  const char* cusername =
    isNull(username) ? empty :CHAR(STRING_ELT(username, 0));

  void *data;
  UInt32 length;
  SEXP result;

  SecKeychainRef keychain =
    isNull(keyring) ? NULL :
    keyring_macos_open_keychain(CHAR(STRING_ELT(keyring, 0)));

  OSStatus status = SecKeychainFindGenericPassword(
    keychain,
    (UInt32) strlen(cservice), cservice,
    (UInt32) strlen(cusername), cusername,
    &length, &data,
    /* itemRef = */ NULL);

  if (keychain != NULL) CFRelease(keychain);

  if (status != errSecSuccess) keyring_macos_error("get", status);

  result = PROTECT(ScalarString(mkCharLen((const char*) data, length)));
  SecKeychainItemFreeContent(NULL, data);

  UNPROTECT(1);
  return result;
}

/* TODO: recode in UTF8 */

SEXP keyring_macos_set(SEXP keyring, SEXP service, SEXP username,
		       SEXP password) {

  const char* empty = "";
  const char* cservice = CHAR(STRING_ELT(service, 0));
  const char* cusername =
    isNull(username) ? empty : CHAR(STRING_ELT(username, 0));
  const char* cpassword = CHAR(STRING_ELT(password, 0));
  SecKeychainItemRef item;

  SecKeychainRef keychain =
    isNull(keyring) ? NULL :
    keyring_macos_open_keychain(CHAR(STRING_ELT(keyring, 0)));

  /* Try to find it, and it is exists, update it */

  OSStatus status = SecKeychainFindGenericPassword(
    keychain,
    (UInt32) strlen(cservice), cservice,
    (UInt32) strlen(cusername), cusername,
    /* passwordLength = */ NULL, /* passwordData = */ NULL,
    &item);

  if (status == errSecItemNotFound) {
    status = SecKeychainAddGenericPassword(
      keychain,
      (UInt32) strlen(cservice), cservice,
      (UInt32) strlen(cusername), cusername,
      (UInt32) strlen(cpassword), cpassword,
      /* itemRef = */ NULL);

  } else {
    status = SecKeychainItemModifyAttributesAndData(
      item,
      /* attrList= */ NULL,
      (UInt32) strlen(cpassword), cpassword);
    CFRelease(item);
  }

  if (keychain != NULL) CFRelease(keychain);

  if (status != errSecSuccess) keyring_macos_error("set", status);

  return R_NilValue;
}

SEXP keyring_macos_delete(SEXP keyring, SEXP service, SEXP username) {

  const char* empty = "";
  const char* cservice = CHAR(STRING_ELT(service, 0));
  const char* cusername =
    isNull(username) ? empty : CHAR(STRING_ELT(username, 0));

  SecKeychainRef keychain =
    isNull(keyring) ? NULL : keyring_macos_open_keychain(CHAR(STRING_ELT(keyring, 0)));
  SecKeychainItemRef item;

  OSStatus status = SecKeychainFindGenericPassword(
    keychain,
    (UInt32) strlen(cservice), cservice,
    (UInt32) strlen(cusername), cusername,
    /* *passwordLength = */ NULL, /* *passwordData = */ NULL,
    &item);

  if (status != errSecSuccess) {
    if (keychain != NULL) CFRelease(keychain);
    keyring_macos_error("delete", status);
  }

  status = SecKeychainItemDelete(item);
  if (status != errSecSuccess) {
    if (keychain != NULL) CFRelease(keychain);
    keyring_macos_error("delete", status);
  }

  if (keychain != NULL) CFRelease(keychain);
  CFRelease(item);

  return R_NilValue;
}

static void keyring_macos_list_item(SecKeychainItemRef item, SEXP result,
				    int idx) {
  SecItemClass class;
  SecKeychainAttribute attrs[] = {
    { kSecServiceItemAttr },
    { kSecAccountItemAttr }
  };
  SecKeychainAttributeList attrList = { 2, attrs };

  if (SecKeychainItemGetTypeID() != CFGetTypeID(item)) {
    SET_STRING_ELT(VECTOR_ELT(result, 0), idx, mkChar(""));
    SET_STRING_ELT(VECTOR_ELT(result, 1), idx, mkChar(""));
    return;
  }

  OSStatus status = SecKeychainItemCopyContent(item, &class, &attrList,
					       /* length = */ NULL,
					       /* outData = */ NULL);
  keyring_macos_handle_status("list", status);
  SET_STRING_ELT(VECTOR_ELT(result, 0), idx,
		 mkCharLen(attrs[0].data, attrs[0].length));
  SET_STRING_ELT(VECTOR_ELT(result, 1), idx,
		 mkCharLen(attrs[1].data, attrs[1].length));
  SecKeychainItemFreeContent(&attrList, NULL);
}

CFArrayRef keyring_macos_list_get(const char *ckeyring,
				  const char *cservice) {

  CFStringRef cfservice = NULL;

  CFMutableDictionaryRef query = CFDictionaryCreateMutable(
    kCFAllocatorDefault, 0,
    &kCFTypeDictionaryKeyCallBacks,
    &kCFTypeDictionaryValueCallBacks);

  CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitAll);
  CFDictionarySetValue(query, kSecReturnData, kCFBooleanFalse);
  CFDictionarySetValue(query, kSecReturnRef, kCFBooleanTrue);
  CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);

  if (ckeyring) {
    SecKeychainRef keychain = keyring_macos_open_keychain(ckeyring);
    CFArrayRef searchList =
      CFArrayCreate(NULL, (const void **) &keychain, 1,
		    &kCFTypeArrayCallBacks);
    CFRelease(searchList);
  }

  if (cservice) {
    cfservice = CFStringCreateWithBytes(
      /* alloc = */ NULL,
      (const UInt8*) cservice, strlen(cservice),
      kCFStringEncodingUTF8,
      /* isExternalRepresentation = */ 0);
      CFDictionarySetValue(query, kSecAttrService, cfservice);
  }

  CFArrayRef resArray = NULL;
  OSStatus status = SecItemCopyMatching(query, (CFTypeRef*) &resArray);
  CFRelease(query);
  if (cfservice != NULL) CFRelease(cfservice);

  if (status != errSecSuccess) {
    if (resArray != NULL) CFRelease(resArray);
    if (status != errSecSuccess) keyring_macos_error("list", status);
    return NULL;

  } else {
    return resArray;
  }
}

SEXP keyring_macos_list(SEXP keyring, SEXP service) {

  const char *ckeyring =
    isNull(keyring) ? NULL : CHAR(STRING_ELT(keyring, 0));
  const char *cservice =
    isNull(service) ? NULL : CHAR(STRING_ELT(service, 0));

  CFArrayRef resArray = keyring_macos_list_get(ckeyring, cservice);
  CFIndex i, num = CFArrayGetCount(resArray);
  SEXP result;
  PROTECT(result = allocVector(VECSXP, 2));
  SET_VECTOR_ELT(result, 0, allocVector(STRSXP, num));
  SET_VECTOR_ELT(result, 1, allocVector(STRSXP, num));
  for (i = 0; i < num; i++) {
    SecKeychainItemRef item =
      (SecKeychainItemRef) CFArrayGetValueAtIndex(resArray, i);
    keyring_macos_list_item(item, result, (int) i);
  }

  CFRelease(resArray);
  UNPROTECT(1);
  return result;
}

SEXP keyring_macos_create(SEXP keyring, SEXP password) {
  const char *ckeyring = CHAR(STRING_ELT(keyring, 0));
  const char *cpassword = CHAR(STRING_ELT(password, 0));

  SecKeychainRef result = NULL;

  OSStatus status = SecKeychainCreate(
    ckeyring,
    /* passwordLength = */ 0, cpassword,
    /* promptUser = */ 0, /* initialAccess = */ NULL,
    &result);

  keyring_macos_handle_status("create", status);

  CFArrayRef keyrings = NULL;
  status = SecKeychainCopyDomainSearchList(
    kSecPreferencesDomainUser,
    &keyrings);

  if (status) {
    if (result != NULL) CFRelease(result);
    keyring_macos_handle_status("create", status);
  }

  /* We need to add the new keychain to the keychain search list,
     otherwise applications like Keychain Access will not see it.
     There is no API to append it, we need to query the current
     search list, add it, and then set the whole new search list.
     This is of course a race condition. :/ */

  CFIndex count = CFArrayGetCount(keyrings);
  CFMutableArrayRef newkeyrings =
    CFArrayCreateMutableCopy(NULL, count + 1, keyrings);
  CFArrayAppendValue(newkeyrings, result);
  status = SecKeychainSetDomainSearchList(
    kSecPreferencesDomainUser,
    newkeyrings);

  if (status) {
    if (result) CFRelease(result);
    if (keyrings) CFRelease(keyrings);
    if (newkeyrings) CFRelease(newkeyrings);
    keyring_macos_handle_status("create", status);
  }

  CFRelease(result);
  CFRelease(keyrings);
  CFRelease(newkeyrings);

  return R_NilValue;
}

SEXP keyring_macos_list_keyring() {
  CFArrayRef keyrings = NULL;
  OSStatus status =
    SecKeychainCopyDomainSearchList(kSecPreferencesDomainUser, &keyrings);
  keyring_macos_handle_status("list_keyrings", status);

  CFIndex i, num = CFArrayGetCount(keyrings);

  SEXP result = PROTECT(allocVector(VECSXP, 3));
  SET_VECTOR_ELT(result, 0, allocVector(STRSXP, num));
  SET_VECTOR_ELT(result, 1, allocVector(INTSXP, num));
  SET_VECTOR_ELT(result, 2, allocVector(LGLSXP, num));

  for (i = 0; i < num; i++) {
    SecKeychainRef keychain =
      (SecKeychainRef) CFArrayGetValueAtIndex(keyrings, i);
    UInt32 pathLength = MAXPATHLEN;
    char pathName[MAXPATHLEN + 1];
    status = SecKeychainGetPath(keychain, &pathLength, pathName);
    pathName[pathLength] = '\0';
    if (status) {
      CFRelease(keyrings);
      keyring_macos_handle_status("list_keyrings", status);
    }
    SET_STRING_ELT(VECTOR_ELT(result, 0), i, mkCharLen(pathName, pathLength));

    CFArrayRef resArray =
      keyring_macos_list_get(pathName, /* cservice = */ NULL);
    CFIndex numitems = CFArrayGetCount(resArray);
    CFRelease(resArray);
    INTEGER(VECTOR_ELT(result, 1))[i] = numitems;

    SecKeychainStatus kstatus;
    status = SecKeychainGetStatus(keychain, &kstatus);
    if (status) {
      LOGICAL(VECTOR_ELT(result, 2))[i] = NA_LOGICAL;
    } else {
      LOGICAL(VECTOR_ELT(result, 2))[i] =
	! (kstatus & kSecUnlockStateStatus);
    }
  }

  CFRelease(keyrings);

  UNPROTECT(1);
  return result;
}

SEXP keyring_macos_delete_keyring(SEXP keyring) {

  const char *ckeyring = CHAR(STRING_ELT(keyring, 0));

  /* Need to remove it from the search list as well */

  CFArrayRef keyrings = NULL;
  OSStatus status = SecKeychainCopyDomainSearchList(
    kSecPreferencesDomainUser,
    &keyrings);
  keyring_macos_handle_status("delete_keyring", status);

  CFIndex i, count = CFArrayGetCount(keyrings);
  CFMutableArrayRef newkeyrings =
    CFArrayCreateMutableCopy(NULL, count, keyrings);
  for (i = 0; i < count; i++) {
    SecKeychainItemRef item =
      (SecKeychainItemRef) CFArrayGetValueAtIndex(keyrings, i);
    UInt32 pathLength = MAXPATHLEN;
    char pathName[MAXPATHLEN + 1];
    status = SecKeychainGetPath(item, &pathLength, pathName);
    pathName[pathLength] = '\0';
    if (status) {
      CFRelease(keyrings);
      CFRelease(newkeyrings);
      keyring_macos_handle_status("delete_keyring", status);
    }
    if (!strcmp(pathName, ckeyring)) {
      CFArrayRemoveValueAtIndex(newkeyrings, (CFIndex) i);
      status = SecKeychainSetDomainSearchList(
        kSecPreferencesDomainUser,
	newkeyrings);
      if (status) {
	CFRelease(keyrings);
	CFRelease(newkeyrings);
	keyring_macos_handle_status("delete_keyring", status);
      }
    }
  }

  /* If we haven't found it on the search list,
     then we just keep silent about it ... */

  CFRelease(keyrings);
  CFRelease(newkeyrings);

  /* And now remove the file as well... */
  SecKeychainRef keychain = keyring_macos_open_keychain(ckeyring);
  status = SecKeychainDelete(keychain);
  CFRelease(keychain);
  keyring_macos_handle_status("delete_keyring", status);

  return R_NilValue;
}

#endif // __APPLE__
