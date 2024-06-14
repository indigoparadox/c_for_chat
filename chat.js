
var sock;
var alerter;

$(document).ready( function() {
   sock = new WebSocket(
      "ws://zarchat.interfinitydynamics.info/chat_sock", "cchat-protocol" );
   alerter = new Audio( '/alert.mp3' );

   $('#send').click( function( e ) {
      /* Send the text to the server as a chat and clear the textbox. */
      /* TODO: Insert destination. */
      sock.send( "PRIVMSG : " + $('#chat').val() );
      $('#chat').val( '' );
      return false;
   } );

   sock.onmessage = function( e ) {
      /* Parse out the protocol fields. */
      const re_msg =
         /^:(?<from>\S*)\s*(?<cmd>[A-Z]*): (?<time>[0-9]*) (?<msg>.*)$/g;
      console.log( "receive: " + e.data );
      let m = re_msg.exec( e.data );

      /* Show privmsgs in the message list. */
      if( "PRIVMSG" == m[2] ) {
         let d = new Date( m[3] * 1000 );

         alerter.play();
         
         $('.chat-messages').prepend( "<tr>" +
               "<td class=\"chat-from\">" + m[1] + "</td>" +
               "<td class=\"chat-msg\">" + m[4] + "</td>" +
               "<td class=\"chat-time\">" + strftime( '%Y-%m-%d %H:%M', d ) + "</td>" +
            "</tr>" );
      }
   };

   sock.onopen = function( e ) {
      console.log( 'socket open!' );
   };

   sock.onclose = function( e ) {
      console.log( 'socket closed!' );
      $('.chat-messages').prop( 'disabled', true );
   };
} );

