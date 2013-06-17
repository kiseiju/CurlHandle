//
//  CURLHandle.h
//  CURLHandle
//
//  Created by Dan Wood <dwood@karelia.com> on Fri Jun 22 2001.
//  Copyright (c) 2013 Karelia Software. All rights reserved.

#import <Foundation/Foundation.h>
#import <curl/curl.h>


#ifndef CURLHandleLog
#define CURLHandleLog(...) // no logging by default - to enable it, add something like this to the prefix: #define CURLHandleLog NSLog
#endif

@class CURLMulti;

@protocol CURLHandleDelegate;

extern NSString * const CURLcodeErrorDomain;
extern NSString * const CURLMcodeErrorDomain;
extern NSString * const CURLSHcodeErrorDomain;

typedef NS_ENUM(NSInteger, CURLHandleState) {
    CURLHandleStateRunning = 0,
    CURLHandleStateCanceling = 2,
    CURLHandleStateCompleted = 3,
};

/**
 Wrapper for a CURL easy handle.
 */

@interface CURLHandle : NSObject
{
	CURL                    *_curl;                         /*" Pointer to the actual CURL object that does all the hard work "*/
    CURLMulti               *_multi;
    NSURL                   *_URL;
	id <CURLHandleDelegate> _delegate;
    CURLHandleState         _state;
    NSError                 *_error;
    
	char                    _errorBuffer[CURL_ERROR_SIZE];	/*" Buffer to hold string generated by CURL; this is then converted to an NSString. "*/
    BOOL                    _executing;                     // debugging
	NSMutableData           *_headerBuffer;                 /*" The buffer that is filled with data from the header as the download progresses; it's appended to one line at a time. "*/
    NSMutableArray          *_lists;                        // Lists we need to hold on to until the handle goes away.
	NSDictionary            *_proxies;                      /*" Dictionary of proxy information; it's released when the handle is deallocated since it's needed for the transfer."*/
    NSInputStream           *_uploadStream;
}

//  Loading respects as many of NSURLRequest's built-in features as possible, including:
//  
//    * An HTTP method of @"HEAD" turns on the CURLOPT_NOBODY option, regardless of protocol (e.g. handy for FTP)
//    * Similarly, @"PUT" turns on the CURLOPT_UPLOAD option (again handy for FTP uploads)
//  
//    * Supply -HTTPBody or -HTTPBodyStream to switch Curl into uploading mode, regardless of protocol
//  
//    * Custom Range: HTTP headers are specially handled to set the CURLOPT_RANGE option, regardless of protocol in use
//      (you should still construct the header as though it were HTTP, e.g. bytes=500-999)
//  
//    * Custom Accept-Encoding: HTTP headers are specially handled to set the CURLOPT_ENCODING option
//
//  Delegate messages are delivered on an arbitrary thread; you should bounce over a specific thread if required for thread safety, or doing any significant work
//
//  Redirects are *not* automatically followed. If you want that behaviour, NSURLConnection is likely a better match for your needs
- (id)initWithRequest:(NSURLRequest *)request credential:(NSURLCredential *)credential delegate:(id <CURLHandleDelegate>)delegate __attribute((nonnull(1)));

@property (readonly, strong) id <CURLHandleDelegate> delegate; // As an asynchronous API, CURLHandle retains its delegate until the request is finished, failed, or cancelled. Much like NSURLConnection

/**
 Stops the request as quickly as possible. Will report back a NSURLErrorCancelled to the delegate
 */

- (void)cancel;

/*
 * The current state of the handle.
 */
@property (readonly) CURLHandleState state;

/*
 * The error, if any, delivered via -handle:didFailWithError:
 * This property will be nil in the event that no error occurred.
 */
@property (readonly, copy) NSError *error;

/**
 CURLINFO_FTP_ENTRY_PATH. Only suitable once handle has finished.
 
 @return The value of CURLINFO_FTP_ENTRY_PATH.
 */

- (NSString *)initialFTPPath;
+ (NSString *)curlVersion;
+ (NSString*)nameForType:(curl_infotype)type;

@end

#pragma mark - Old API

@interface CURLHandle(OldAPI)

/** @name Synchronous Methods */

/**
 Perform a request synchronously.

 Please don't use this unless you have to!
 To use, -init a handle, and then call this method, as many times as you like. Delegate messages will be delivered fairly normally during the request
 To cancel a synchronous request, call -cancel on a different thread and this method will return as soon as it can
 
 @param request The request to perform.
 @param credential A credential to use for the request.
 @param delegate An object to use as the delegate.
 */

- (void)sendSynchronousRequest:(NSURLRequest *)request credential:(NSURLCredential *)credential delegate:(id <CURLHandleDelegate>)delegate;

+ (void) setProxyUserIDAndPassword:(NSString *)inString;
+ (void) setAllowsProxy:(BOOL) inBool;
@end

#pragma mark - Delegate

/**
 Protocol that should be implemented by delegates of CURLHandle.
 */

@protocol CURLHandleDelegate <NSObject>

/**
 Required protocol method, called when data is received.
 
 @param handle The handle receiving the data.
 @param data The new data.
 */

- (void)handle:(CURLHandle *)handle didReceiveData:(NSData *)data;

@optional

/**
 Optional method, called when a response is received.
 
 @param handle The handle receiving the response.
 @param response The response.
 */

- (void)handle:(CURLHandle *)handle didReceiveResponse:(NSURLResponse *)response;

/** 
 Optional method, called when the handle has successfully completed.
 
 @param handle The handle that has completed.
 */

- (void)handleDidFinish:(CURLHandle *)handle;

/**
 Optional method, called when the handle has failed.

 Where possible errors are in NSURLErrorDomain or NSCocoaErrorDomain. 
 
 There will generally be a CURLcodeErrorDomain error present; either directly, or as an underlying 
 error (KSError <https://github.com/karelia/KSError> is handy for querying underlying errors).

 The key CURLINFO_RESPONSE_CODE (as an NSNumber) will be filled out with HTTP/FTP status code if appropriate.

 At present all errors include NSURLErrorFailingURLErrorKey and NSURLErrorFailingURLStringErrorKey if applicable even
 though the docs say "This key is only present in the NSURLErrorDomain". Should we respect that?
 
 @param handle The handle that has failed.
 @param error The error that it failed with.
 */

- (void)handle:(CURLHandle*)handle didFailWithError:(NSError*)error;

/**
 Optional method, called to ask how to handle a host fingerprint.

 If not implemented, only matching keys are accepted; all else is rejected
 I've found that CURLKHSTAT_FINE_ADD_TO_FILE only bothers appending to the file if not already present

 @param handle The handle that's found the fingerprint
 @param foundKey The fingerprint.
 @param knownkey The known fingerprint for the host.
 @param match Whether the fingerprints matched.
 @return A status value indicating what to do.

 */

- (enum curl_khstat)handle:(CURLHandle *)handle didFindHostFingerprint:(const struct curl_khkey *)foundKey knownFingerprint:(const struct curl_khkey *)knownkey match:(enum curl_khmatch)match;

/**
 Optional method, called just before a handle sends some data.

 Reports a length of 0 when the end of the data is written so you can get a nice heads up that an upload is about to complete.
 
 @param handle The handle that is sending data.
 @param bytesWritten The amount of data to be sent. This will be zero when the last data has been written.
 */

- (void)handle:(CURLHandle *)handle willSendBodyDataOfLength:(NSUInteger)bytesWritten;

/**
 Optional method, called to report debug/status information to the delegate.
 
 @param handle The handle that the information relates to.
 @param string The information string.
 @param type The type of information.
 */

- (void)handle:(CURLHandle *)handle didReceiveDebugInformation:(NSString *)string ofType:(curl_infotype)type;

@end

#pragma mark - Error Domains

extern NSString * const CURLcodeErrorDomain;
extern NSString * const CURLMcodeErrorDomain;
extern NSString * const CURLSHcodeErrorDomain;

/** 
 CURLHandle support.
 */

@interface NSError(CURLHandle)

/**
 Returns the response code from CURL, if one was set on the error.
 
 @return The response code, or zero if there is no error or no response code was set.
 */

- (NSUInteger)curlResponseCode;
@end

