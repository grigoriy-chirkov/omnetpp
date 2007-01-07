#include "JMessage.h"
#include "JSimpleModule.h"

//#define DEBUGPRINTF printf
#define DEBUGPRINTF (void)

#define JENV   JSimpleModule::jenv


JMessage::JMessage(const char *name, int kind, int) : cMessage(name, kind)
{
    javaPeer = 0;
    cloneMethod = 0;
}

JMessage::JMessage(const JMessage& msg)
{
    javaPeer = 0;
    cloneMethod = 0;
    operator=(msg);
}

JMessage::~JMessage()
{
    if (JENV && javaPeer)
        JENV->DeleteGlobalRef(javaPeer);
}

JMessage& JMessage::operator=(const JMessage& msg)
{
    cMessage::operator=(msg);
    if (!JENV)
        return *this;

    if (javaPeer)
        JENV->DeleteGlobalRef(javaPeer);
    javaPeer = 0;
    cloneMethod = 0;
    if (msg.javaPeer)
    {
        // must clone() the other Java object
        javaPeer = msg.javaPeer;  // then we'll clone it
        cloneMethod = msg.cloneMethod;
        if (!cloneMethod)
        {
            jclass clazz = JENV->GetObjectClass(javaPeer);
            cloneMethod = JENV->GetMethodID(clazz, "clone", "()Ljava/lang/Object;");
            const_cast<JMessage&>(msg).cloneMethod = cloneMethod;
            checkExceptions();
        }
        javaPeer = JENV->CallObjectMethod(javaPeer, cloneMethod);
        checkExceptions();
        javaPeer = JENV->NewGlobalRef(javaPeer);
        checkExceptions();
    }
    return *this;
}

void JMessage::swigSetJavaPeer(jobject msgObject)
{
    ASSERT(javaPeer==0);
    this->javaPeer = JENV->NewGlobalRef(msgObject);
}

static std::string fromJavaString(jstring stringObject)
{
    jboolean isCopy;
    const char *buf = JENV->GetStringUTFChars(stringObject, &isCopy);
    std::string str = buf ? buf : "";
    JENV->ReleaseStringUTFChars(stringObject, buf);
    return str;
}

static jstring toJavaString(const char *s)
{
    return JENV->NewStringUTF(s);
}

void JMessage::checkExceptions() const
{
    jthrowable exceptionObject = JENV->ExceptionOccurred();
    if (exceptionObject)
    {
        DEBUGPRINTF("JSimpleModule: exception occurred:\n");
        JENV->ExceptionDescribe();
        JENV->ExceptionClear();

        jclass throwableClass = JENV->FindClass("java/lang/Throwable");
        jmethodID getMessageMethod = JENV->GetMethodID(throwableClass, "getMessage", "()Ljava/lang/String;");
        jstring msg = (jstring)JENV->CallObjectMethod(exceptionObject, getMessageMethod);
        opp_error("%s", fromJavaString(msg).c_str());
    }
}

jobject JMessage::swigJavaPeerOf(cObject *object)
{
    JMessage *msg = dynamic_cast<JMessage *>(object);
    return msg ? msg->swigJavaPeer() : 0;
}

void JMessage::getMethodOrField(const char *fieldName, const char *methodPrefix,
                                const char *methodsig, const char *fieldsig,
                                jmethodID& methodID, jfieldID& fieldID) const
{
    jclass clazz = JENV->GetObjectClass(javaPeer);
    checkExceptions();
    fieldID = 0;
    std::string methodName = std::string(methodPrefix)+fieldName; //XXX toupper first char!
    methodID = JENV->GetMethodID(clazz, methodName.c_str(), methodsig);
    if (methodID)
        return;
    JENV->ExceptionClear();
    fieldID = JENV->GetFieldID(clazz, fieldName, fieldsig);
    if (fieldID)
        return;
    JENV->ExceptionClear();
    opp_error("(%s)%s: neither method `%s' nor field `%s' found in the Java object",
              className(), fullName(), methodName.c_str(), fieldName);
}

#define GETTER_SETTER(Type, jtype, CODE)  \
  jtype JMessage::get##Type##JavaField(const char *fieldName) const \
  { \
      jmethodID methodID; jfieldID fieldID; \
      getMethodOrField(fieldName, "get", "()" CODE, CODE, methodID, fieldID); \
      return checkException(methodID ? JENV->Call##Type##Method(javaPeer, methodID) : JENV->Get##Type##Field(javaPeer, fieldID)); \
  } \
  void JMessage::set##Type##JavaField(const char *fieldName, jtype value) \
  { \
      jmethodID methodID; jfieldID fieldID; \
      getMethodOrField(fieldName, "set", "(" CODE ")V", CODE, methodID, fieldID); \
      methodID ? JENV->Call##Type##Method(javaPeer, methodID, value) : JENV->Set##Type##Field(javaPeer, fieldID, value); \
      checkExceptions(); \
  }

GETTER_SETTER(Boolean, jboolean, "Z")
GETTER_SETTER(Byte,    jbyte,    "B")
GETTER_SETTER(Char,    jchar,    "C")
GETTER_SETTER(Short,   jshort,   "S")
GETTER_SETTER(Int,     jint,     "I")
GETTER_SETTER(Long,    jlong,    "J")
GETTER_SETTER(Float,   jfloat,   "F")
GETTER_SETTER(Double,  jdouble,  "D")

//TODO methods for wrapping/unwrapping strings
#define JSTRING "Ljava/lang/String;"


std::string JMessage::getStringJavaField(const char *fieldName) const
{
   jmethodID methodID; jfieldID fieldID;
   getMethodOrField(fieldName, "get", "()" JSTRING, JSTRING, methodID, fieldID);
   jstring str = (jstring) checkException(methodID ? JENV->CallObjectMethod(javaPeer, methodID) : JENV->GetObjectField(javaPeer, fieldID));
   return fromJavaString(str);
}

void JMessage::setStringJavaField(const char *fieldName, const char *value)
{
   jmethodID methodID; jfieldID fieldID;
   getMethodOrField(fieldName, "set", "(" JSTRING ")V", JSTRING, methodID, fieldID);
   jstring str = JENV->NewStringUTF(value);
   methodID ? JENV->CallObjectMethod(javaPeer, methodID, str) : JENV->SetObjectField(javaPeer, fieldID, str);
   checkExceptions();
}



