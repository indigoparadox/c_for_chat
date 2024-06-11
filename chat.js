
var sock;
const re_msg = /^:(?<from>\S*)\s*(?<cmd>[A-Z]*): (?<time>[0-9]*) (?<msg>.*)$/g;

$(document).ready( function() {
   sock = new WebSocket(
      "ws://zarchat.interfinitydynamics.info/chat_sock", "cchat-protocol" );

   sock.onmessage = function( e ) {
      console.log( "receive: " + e.data );
      m = re_msg.exec( e.data );
      console.log( m );
      if( "PRIVMSG" == m[2] ) {
         $('.chat-messages').prepend( "<tr>" +
               "<td class=\"chat-from\">" + m[1] + "</td>" +
               "<td class=\"chat-msg\">" + m[4] + "</td>" +
               "<td class=\"chat-time\">" + m [3] + "</td>" +
            "</tr>" );
      }
   };

   sock.onopen = function( e ) {
      console.log( 'socket open!' );
   };
} );

