#include "Licence.h"
#include <juce_cryptography/juce_cryptography.h>
#include <atomic>
#include <mutex>

extern "C"
{
#include "tweetnacl.h"
    // TweetNaCl wants this for key generation, which plugins never do (we only verify).
    void randombytes (unsigned char* p, unsigned long long n) { for (unsigned long long i = 0; i < n; ++i) p[i] = 0; }
}

namespace hz
{
// Public half of the 99Hertz licence signing key (the secret half never ships).
static const char* publicKeyHex = "047cfd66e3c22a0350631b2884d09092ce18efe8ea1c4870269e90cbd1f18fbe";

static juce::MemoryBlock fromBase64Url (juce::String s)
{
    s = s.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0) s << "=";
    juce::MemoryOutputStream out;
    if (! juce::Base64::convertFromBase64 (out, s)) return {};
    return out.getMemoryBlock();
}

// "<prefix><base64url(json)>.<base64url(sig)>" signed by our key -> the json, or a void var.
static juce::var openSigned (juce::String s, const char* prefix)
{
    if (! s.startsWith (prefix)) return {};
    s = s.substring ((int) std::strlen (prefix));
    const auto dot = s.indexOfChar ('.');
    if (dot <= 0) return {};

    const auto payload = fromBase64Url (s.substring (0, dot));
    const auto sig     = fromBase64Url (s.substring (dot + 1));
    if (payload.getSize() == 0 || sig.getSize() != 64) return {};

    juce::MemoryBlock pk;
    pk.loadFromHexString (publicKeyHex);
    if (pk.getSize() != 32) return {};

    // crypto_sign_open wants signature || message
    std::vector<unsigned char> sm (64 + payload.getSize()), m (sm.size());
    std::memcpy (sm.data(), sig.getData(), 64);
    std::memcpy (sm.data() + 64, payload.getData(), payload.getSize());
    unsigned long long mlen = 0;
    if (crypto_sign_open (m.data(), &mlen, sm.data(), (unsigned long long) sm.size(), (const unsigned char*) pk.getData()) != 0)
        return {};
    const auto json = juce::JSON::parse (payload.toString());
    return json.isObject() ? json : juce::var();
}

static juce::String sha256Hex (const juce::String& s)
{
    return juce::SHA256 (s.toUTF8()).toHexString().removeCharacters (" ").toLowerCase();
}

static const juce::String midDot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

// ── LicenceInfo ───────────────────────────────────────────────────────────────
int LicenceInfo::daysLeft() const
{
    if (expires == 0) return 9999;
    // 5 minutes' grace, so a trial issued a moment ago shows 7 days, not 8.
    const auto secs = expires - juce::Time::currentTimeMillis() / 1000 - 300;
    return secs <= 0 ? 0 : (int) ((secs + 86399) / 86400);
}

juce::String LicenceInfo::daysLeftText() const
{
    const auto d = daysLeft();
    return juce::String (d) + (d == 1 ? " DAY LEFT" : " DAYS LEFT");
}

juce::String LicenceInfo::buttonText() const
{
    switch (kind)
    {
        case pro:             return "LICENSED";
        case needsActivation: return "ACTIVATE";
        case trial:           return "TRIAL" + midDot + daysLeftText() + midDot + "UPGRADE";
        case trialExpired:    return "TRIAL ENDED" + midDot + "UPGRADE";
        default:              return "UPGRADE";
    }
}

juce::String LicenceInfo::ownerText() const
{
    return email + (order > 0 ? midDot + "ORDER #" + juce::String (order) : juce::String());
}

juce::String LicenceInfo::licensedText() const
{
    return "LICENSED TO " + email.toUpperCase() + (order > 0 ? midDot + "ORDER #" + juce::String (order) : juce::String());
}

// ── Files ─────────────────────────────────────────────────────────────────────
static juce::File licenceDir()
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #endif
    return dir.getChildFile ("99Hertz").getChildFile ("Licences");
}

static juce::File keyFileFor (const juce::String& product)   { return licenceDir().getChildFile (product + ".key"); }
static juce::File tokenFileFor (const juce::String& product) { return licenceDir().getChildFile (product + ".act"); }
static juce::File allTokenFile()                             { return licenceDir().getChildFile ("all.act"); }

static bool hasToken (const juce::String& product, const juce::String& key)
{
    const auto mid = Licence::machineId();
    for (auto f : { tokenFileFor (product), allTokenFile() })
        if (f.existsAsFile() && Licence::tokenMatches (f.loadFileAsString(), key, product, mid))
            return true;
    return false;
}

static LicenceInfo checkFor (const juce::String& product)
{
    const auto f = keyFileFor (product);
    if (! f.existsAsFile()) return {};
    const auto key = f.loadFileAsString();
    auto info = Licence::verify (key, product);
    if (info.kind == LicenceInfo::pro && ! hasToken (product, Licence::cleanKey (key)))
        info.kind = LicenceInfo::needsActivation;
    return info;
}

static LicenceInfo applyFor (const juce::String& product, const juce::String& key)
{
    auto info = Licence::verify (key, product);
    if (info.kind == LicenceInfo::invalid || info.kind == LicenceInfo::none) return info;

    // Never let a locked key (an ended trial) replace an unlocked one, or a trial replace a pro key.
    const auto current = checkFor (product);
    if (current.unlocked() && ! info.unlocked()) return current;
    if (current.isProKey() && info.kind != LicenceInfo::pro) return current;

    const auto f = keyFileFor (product);
    const auto clean = Licence::cleanKey (key);
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (clean)) { info.kind = LicenceInfo::saveFailed; return info; }

    // Read it back: if the folder silently refused the write, say so rather than unlock until restart.
    if (! Licence::verify (f.loadFileAsString(), product).sameAs (info)) { info.kind = LicenceInfo::saveFailed; return info; }
    return checkFor (product);
}

// A pro key the server just activated: token first, then the key, so another instance never sees the
// new key without its token for longer than a poll.
static LicenceInfo saveActivated (const juce::String& product, const juce::String& key, const juce::String& token)
{
    const auto json = openSigned (token.trim(), "99HA-");
    const auto tf = json["p"].toString() == "all" ? allTokenFile() : tokenFileFor (product);
    tf.getParentDirectory().createDirectory();
    LicenceInfo failed = Licence::verify (key, product);
    failed.kind = LicenceInfo::saveFailed;
    if (! tf.replaceWithText (token.trim())) return failed;
    const auto info = applyFor (product, key);
    return info.kind == LicenceInfo::pro ? info : failed;
}

// ── Machine ───────────────────────────────────────────────────────────────────
juce::String Licence::machineId()
{
    static const juce::String id = []
    {
        auto dev = juce::SystemStats::getUniqueDeviceID().trim();
       #if JUCE_WINDOWS
        if (dev.isEmpty())
            dev = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid",
                                                   {}, juce::WindowsRegistry::WoW64_64bit).trim();
       #endif
        if (dev.isEmpty())   // never seen on a real computer; still stable for this computer and user
            dev = "fallback|" + juce::SystemStats::getComputerName() + "|" + juce::SystemStats::getLogonName();
        return sha256Hex ("99hz|" + dev).substring (0, 32);
    }();
    return id;
}

juce::String Licence::machineName()
{
    auto n = juce::SystemStats::getComputerName().upToLastOccurrenceOf (".local", false, true).trim();
    if (n.isEmpty()) n = juce::SystemStats::getOperatingSystemName();
    return n.substring (0, 64);
}

static juce::String& serverBaseRef() { static juce::String s ("https://99hertz.com/api/plugin/"); return s; }
static std::mutex serverLock;
juce::String Licence::serverBase()                               { std::lock_guard<std::mutex> l (serverLock); return serverBaseRef(); }
void Licence::setServerBaseForTests (const juce::String& base)   { std::lock_guard<std::mutex> l (serverLock); serverBaseRef() = base; }

static std::atomic<int> inFlight { 0 };
int Licence::requestsInFlight() { return inFlight.load(); }

// ── Network ───────────────────────────────────────────────────────────────────
namespace detail
{
// One per plugin binary while any Licence exists. Requests run on its pool; when the last Licence goes
// (the plugin closes) it cancels whatever is in flight and waits, so no worker thread can outlive the
// plugin's code being unloaded.
struct LicenceNet
{
    ~LicenceNet()
    {
        {
            const juce::ScopedLock sl (lock);
            closing = true;
            for (auto* s : live) s->cancel();
        }
        if (pool != nullptr) pool->removeAllJobs (true, 20000);
        pool.reset();
    }

    void run (std::function<void (LicenceNet&)> job)
    {
        const juce::ScopedLock sl (lock);
        if (closing) return;
        if (pool == nullptr)
            pool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions{}.withThreadName ("99Hertz licence").withNumberOfThreads (2));
        ++inFlight;
        pool->addJob ([this, job] { job (*this); --inFlight; });
    }

    // POSTs JSON. reached = the server answered (any HTTP status); the reply is the parsed body.
    juce::var post (const juce::String& endpoint, const juce::var& body, bool& reached)
    {
        reached = false;
        juce::WebInputStream s (juce::URL (Licence::serverBase() + endpoint).withPOSTData (juce::JSON::toString (body, true)), true);
        s.withExtraHeaders ("Content-Type: application/json").withConnectionTimeout (12000);
        {
            const juce::ScopedLock sl (lock);
            if (closing) return {};
            live.add (&s);
        }
        const bool connected = s.connect (nullptr);
        juce::String text;
        if (connected && s.getStatusCode() > 0)
        {
            reached = true;
            text = s.readEntireStreamAsString();
        }
        {
            const juce::ScopedLock sl (lock);
            live.removeFirstMatchingValue (&s);
            if (closing) { reached = false; return {}; }
        }
        return juce::JSON::parse (text);
    }

    void deliver (const Licence::Callback& done, const LicenceInfo& info, const juce::String& error)
    {
        const juce::ScopedLock sl (lock);
        if (closing || done == nullptr) return;
        juce::MessageManager::callAsync ([done, info, error] { done (info, error); });
    }

    static std::shared_ptr<LicenceNet> get()
    {
        static std::mutex m;
        static std::weak_ptr<LicenceNet> shared;
        std::lock_guard<std::mutex> l (m);
        auto p = shared.lock();
        if (p == nullptr) { p = std::make_shared<LicenceNet>(); shared = p; }
        return p;
    }

    juce::CriticalSection lock;
    juce::Array<juce::WebInputStream*> live;
    bool closing = false;
    std::unique_ptr<juce::ThreadPool> pool;
};
} // namespace detail

static const char* osName()
{
   #if JUCE_MAC
    return "mac";
   #else
    return "windows";
   #endif
}

static const char* offlineError = "Connect to the internet once to activate";

static void activateJob (detail::LicenceNet& net, const juce::String& product, const juce::String& key, const Licence::Callback& done)
{
    const auto mid = Licence::machineId();
    juce::DynamicObject::Ptr body (new juce::DynamicObject());
    body->setProperty ("key", key);
    body->setProperty ("product", product);
    body->setProperty ("mid", mid);
    body->setProperty ("machine", Licence::machineName());
    body->setProperty ("os", osName());

    bool reached = false;
    const auto reply = net.post ("activate.php", juce::var (body.get()), reached);
    LicenceInfo info;
    juce::String error;
    if (! reached)
        error = offlineError;
    else if (! reply.isObject())
        error = "Couldn't activate. Try again or email info@99hertz.com.";   // e.g. an HTML error page
    else if (! (bool) reply["ok"])
    {
        error = reply["error"].toString();
        if (error.isEmpty() && reply["code"].toString() == "limit")
        {
            const auto machine = reply["machines"][0]["machine"].toString();
            error = "This key is already in use on another computer" + (machine.isNotEmpty() ? " (" + machine + ")" : juce::String())
                  + ". Move it from there first, or email info@99hertz.com.";
        }
        if (error.isEmpty()) error = "Couldn't activate. Try again or email info@99hertz.com.";
    }
    else
    {
        const auto token = reply["token"].toString().trim();
        if (! Licence::tokenMatches (token, key, product, mid))
            error = "Couldn't activate this computer. Email info@99hertz.com.";
        else
        {
            info = saveActivated (product, key, token);
            if (info.kind == LicenceInfo::saveFailed) error = "Couldn't save your key. Email info@99hertz.com.";
        }
    }
    if (error.isNotEmpty() && info.kind != LicenceInfo::saveFailed) info = checkFor (product);
    net.deliver (done, info, error);
}

// ── Licence ───────────────────────────────────────────────────────────────────
Licence::Licence (juce::String productId) : product (std::move (productId)), net (detail::LicenceNet::get()) {}
Licence::Licence (const Licence& o) : product (o.product), net (o.net) {}
Licence::~Licence() = default;

juce::String Licence::cleanKey (const juce::String& pasted)
{
    auto s = pasted;
    const auto start = s.indexOf ("99HZ-");
    if (start > 0) s = s.substring (start);
    s = s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.");
    while (s.endsWithChar ('.')) s = s.dropLastCharacters (1);
    return s;
}

LicenceInfo Licence::verify (const juce::String& rawKey, const juce::String& productId)
{
    LicenceInfo info;
    const auto key = cleanKey (rawKey);
    if (key.isEmpty()) return info;
    info.kind = LicenceInfo::invalid;
    const auto json = openSigned (key, "99HZ-");
    if (! json.isObject()) return info;
    const auto p = json["p"].toString();
    if (p != productId && p != "all") return info;

    info.product = p;
    info.email   = json["e"].toString();
    info.expires = (juce::int64) json["x"];
    info.order   = json.hasProperty ("o") ? (juce::int64) json["o"] : 0;
    const bool isTrial = json["t"].toString() == "trial";
    if (! isTrial) info.kind = LicenceInfo::pro;
    else           info.kind = info.daysLeft() > 0 ? LicenceInfo::trial : LicenceInfo::trialExpired;
    return info;
}

bool Licence::tokenMatches (const juce::String& token, const juce::String& key, const juce::String& productId, const juce::String& mid)
{
    const auto json = openSigned (token.trim(), "99HA-");
    if (! json.isObject() || mid.isEmpty()) return false;
    const auto p = json["p"].toString();
    return json["k"].toString() == sha256Hex (cleanKey (key))
        && json["m"].toString() == mid
        && (p == productId || p == "all");
}

juce::File Licence::keyFile() const       { return keyFileFor (product); }
juce::File Licence::tokenFile() const     { return tokenFileFor (product); }
juce::File Licence::allTokenFile() const  { return hz::allTokenFile(); }
LicenceInfo Licence::check() const        { return checkFor (product); }
LicenceInfo Licence::apply (const juce::String& key) const { return applyFor (product, key); }

void Licence::requestTrial (const juce::String& email, bool subscribe, Callback done) const
{
    const auto prod = product;
    net->run ([prod, email, subscribe, done] (detail::LicenceNet& n)
    {
        juce::DynamicObject::Ptr body (new juce::DynamicObject());
        body->setProperty ("email", email.trim());
        body->setProperty ("product", prod);
        body->setProperty ("subscribe", subscribe);
        body->setProperty ("os", osName());

        juce::String error;
        LicenceInfo info;
        bool reached = false;
        const auto reply = n.post ("trial.php", juce::var (body.get()), reached);
        if (! reached)
            error = "Couldn't reach 99hertz.com. Check your internet and try again.";
        else if (! reply.isObject())
            error = "Couldn't start the trial. Try again or email info@99hertz.com.";   // e.g. an HTML error page
        else if ((bool) reply["ok"])
        {
            info = applyFor (prod, reply["key"].toString());
            if (info.kind == LicenceInfo::saveFailed)
                error = "Couldn't save your key. Email info@99hertz.com.";
            else if (! info.unlocked())
                error = info.kind == LicenceInfo::trialExpired ? "Your free trial for this email has already ended."
                                                               : "Something went wrong with the trial key. Email info@99hertz.com.";
        }
        else
            error = reply["error"].toString().isNotEmpty() ? reply["error"].toString()
                                                           : "Couldn't start the trial. Try again or email info@99hertz.com.";
        n.deliver (done, info, error);
    });
}

void Licence::unlock (const juce::String& rawKey, Callback done) const
{
    const auto key = cleanKey (rawKey);
    const auto pasted = verify (key, product);
    auto finish = [&done] (const LicenceInfo& i, const juce::String& e) { if (done) done (i, e); };

    if (pasted.kind == LicenceInfo::none || pasted.kind == LicenceInfo::invalid)
        return finish (pasted, "That key isn't valid for this plugin");

    if (pasted.kind != LicenceInfo::pro || hasToken (product, key))
    {
        const auto info = apply (key);
        if (info.kind == LicenceInfo::saveFailed)       return finish (info, "Couldn't save your key. Email info@99hertz.com");
        if (pasted.kind == LicenceInfo::trialExpired)   return finish (info, "That trial key has ended");
        return finish (info, {});
    }

    const auto prod = product;
    net->run ([prod, key, done] (detail::LicenceNet& n) { activateJob (n, prod, key, done); });
}

void Licence::activate (Callback done) const
{
    const auto f = keyFile();
    const auto key = f.existsAsFile() ? cleanKey (f.loadFileAsString()) : juce::String();
    if (! verify (key, product).isProKey())
    {
        if (done) done (check(), "Paste your key first");
        return;
    }
    if (hasToken (product, key))
    {
        if (done) done (check(), {});
        return;
    }
    const auto prod = product;
    net->run ([prod, key, done] (detail::LicenceNet& n) { activateJob (n, prod, key, done); });
}

void Licence::deactivate (Callback done) const
{
    const auto f = keyFile();
    const auto key = f.existsAsFile() ? cleanKey (f.loadFileAsString()) : juce::String();
    if (! verify (key, product).isProKey())
    {
        if (done) done (check(), "There's no licence to move");
        return;
    }
    const auto prod = product;
    net->run ([prod, key, done] (detail::LicenceNet& n)
    {
        juce::DynamicObject::Ptr body (new juce::DynamicObject());
        body->setProperty ("key", key);
        body->setProperty ("mid", machineId());
        bool reached = false;
        const auto reply = n.post ("deactivate.php", juce::var (body.get()), reached);
        juce::String error;
        if (! reached)
            error = "Connect to the internet to move your licence";
        else if (! reply.isObject() || ! (bool) reply["ok"])
            error = reply["error"].toString().isNotEmpty() ? reply["error"].toString()
                                                           : "Couldn't move your licence. Try again or email info@99hertz.com.";
        else
        {
            // Released on the server: forget this computer's activation for this key (keep the key).
            const auto mid = machineId();
            for (auto tf : { tokenFileFor (prod), hz::allTokenFile() })
                if (tf.existsAsFile() && tokenMatches (tf.loadFileAsString(), key, prod, mid))
                    tf.deleteFile();
        }
        n.deliver (done, checkFor (prod), error);
    });
}

// ── LicenceWatcher ────────────────────────────────────────────────────────────
LicenceWatcher::LicenceWatcher (const Licence& l, LicenceInfo current, std::function<void (const LicenceInfo&)> cb,
                                int pollMs, int fullCheckMs)
    : licence (l), onChange (std::move (cb)), last (std::move (current)), fullMs (fullCheckMs)
{
    lastDays = last.daysLeft();
    files = stamp();
    lastFull = juce::Time::getMillisecondCounter();
    startTimer (pollMs);
}

LicenceWatcher::~LicenceWatcher() { stopTimer(); }

juce::String LicenceWatcher::stamp() const
{
    juce::String s;
    for (auto f : { licence.keyFile(), licence.tokenFile(), licence.allTokenFile() })
    {
        const auto size = f.existsAsFile() ? f.getSize() : (juce::int64) -1;
        s << size << ":" << (size >= 0 ? f.getLastModificationTime().toMilliseconds() : (juce::int64) 0) << ";";
    }
    return s;
}

void LicenceWatcher::poll (bool force)
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto st = stamp();
    const bool fileChanged = st != files;
    files = st;
    const bool due = now - lastFull >= (juce::uint32) fullMs;
    if (! (force || fileChanged || due)) return;
    lastFull = now;

    const auto info = licence.check();
    const auto days = info.daysLeft();
    if (info.sameAs (last) && days == lastDays) return;
    last = info; lastDays = days;
    if (onChange) onChange (info);
}
} // namespace hz
