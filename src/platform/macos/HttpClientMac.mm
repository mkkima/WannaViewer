#include "wannaviewer/network/HttpClient.hpp"

#include <stdexcept>

#import <Foundation/Foundation.h>

namespace wannaviewer {

HttpResponse HttpClient::Get(const HttpRequest& request, std::stop_token stopToken) const {
    if (!request.url.IsHttp()) throw std::invalid_argument("Only HTTP(S) URLs are supported");
    if (request.url.IsPrivateHostLiteral())
        throw std::invalid_argument("Private or loopback host literals are not accepted by page resolvers");
    @autoreleasepool {
        NSString* text = [NSString stringWithUTF8String:request.url.Value().c_str()];
        NSURL* url = [NSURL URLWithString:text];
        if (!url) throw std::invalid_argument("Invalid URL");
        NSMutableURLRequest* nativeRequest = [NSMutableURLRequest requestWithURL:url];
        nativeRequest.HTTPMethod = @"GET";
        nativeRequest.timeoutInterval = std::chrono::duration<double>(request.receiveTimeout).count();
        for (const auto& [name, value] : request.headers) {
            if (name.find_first_of("\r\n:") != std::string::npos || value.find_first_of("\r\n") != std::string::npos)
                throw std::invalid_argument("Unsafe HTTP header");
            [nativeRequest setValue:[NSString stringWithUTF8String:value.c_str()]
                 forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
        }
        NSURLSessionConfiguration* configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.HTTPMaximumConnectionsPerHost = 2;
        configuration.HTTPShouldSetCookies = NO;
        NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration];
        dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        __block NSData* receivedData = nil;
        __block NSHTTPURLResponse* receivedResponse = nil;
        __block NSError* receivedError = nil;
        NSURLSessionDataTask* task = [session dataTaskWithRequest:nativeRequest completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
            receivedData = [data retain];
            receivedResponse = [(NSHTTPURLResponse*)response retain];
            receivedError = [error retain];
            dispatch_semaphore_signal(completed);
        }];
        std::stop_callback cancellation(stopToken, [task] { [task cancel]; });
        [task resume];
        const auto timeout = dispatch_time(DISPATCH_TIME_NOW,
            static_cast<std::int64_t>(request.receiveTimeout.count()) * NSEC_PER_MSEC);
        if (dispatch_semaphore_wait(completed, timeout) != 0) {
            [task cancel];
            [session invalidateAndCancel];
            throw std::runtime_error("HTTP request timed out");
        }
        [session finishTasksAndInvalidate];
        if (stopToken.stop_requested()) throw std::runtime_error("HTTP request cancelled");
        if (receivedError) {
            const std::string message = receivedError.localizedDescription.UTF8String ?: "HTTP request failed";
            [receivedData release]; [receivedResponse release]; [receivedError release];
            throw std::runtime_error(message);
        }
        if (receivedData.length > request.maximumResponseBytes) {
            [receivedData release]; [receivedResponse release];
            throw std::runtime_error("HTTP response exceeds the size limit");
        }
        HttpResponse result;
        result.status = static_cast<unsigned>(receivedResponse.statusCode);
        result.finalUrl = receivedResponse.URL.absoluteString.UTF8String ?: request.url.Value();
        result.contentType = receivedResponse.MIMEType.UTF8String ?: "";
        result.body.assign(static_cast<const char*>(receivedData.bytes), receivedData.length);
        [receivedData release]; [receivedResponse release];
        return result;
    }
}

} // namespace wannaviewer
