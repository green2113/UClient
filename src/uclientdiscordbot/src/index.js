// Env: DISCORD_TOKEN, DISCORD_APPLICATION_ID, DISCORD_INTERNAL_SECRET
// Optional: UCLIENT_API_BASE_URL (default https://uclient.under1111.com), DISCORD_GUILD_ID
// The bot needs the Message Content intent to read channel messages.
import {
	ChannelType,
	Client,
	GatewayIntentBits,
	MessageFlags,
	PermissionFlagsBits,
	REST,
	Routes,
	SlashCommandBuilder,
} from "discord.js";

const token = process.env.DISCORD_TOKEN ?? "";
const applicationId = process.env.DISCORD_APPLICATION_ID ?? "";
const apiBase = (process.env.UCLIENT_API_BASE_URL ?? "https://uclient.under1111.com").replace(/\/$/, "");
const internalSecret = process.env.DISCORD_INTERNAL_SECRET ?? "";
const guildId = process.env.DISCORD_GUILD_ID ?? "";

const text = {
	notLinked: "Your UClient account is not linked. Use /link first.",
	noChannel: "There is no message channel.",
	channelExists: "A message channel already exists. Delete it with /delete-message-channel first.",
	notInGame: "You are not connected to a game server.",
	openLink: "Open this link to link your UClient account:",
	created: "Created the message channel.",
	deleted: "Deleted the message channel.",
	createFailed: "Could not create the channel. The bot needs permission to manage channels.",
	deleteFailed: "Could not delete the channel.",
	guildOnly: "Use this command in a server.",
	sent: "Sent.",
	emptyMessage: "Message is empty.",
	modeRoomConflict: "Clear the mode, or set it to UClient, when a room is selected.",
};

if(!token || !applicationId || !internalSecret) {
	console.error("DISCORD_TOKEN, DISCORD_APPLICATION_ID, and DISCORD_INTERNAL_SECRET are required.");
	process.exit(1);
}

function channelName(raw) {
	const name = raw.toLowerCase().trim()
		.replace(/\s+/g, "-")
		.replace(/[^a-z0-9-_]/g, "")
		.replace(/-+/g, "-")
		.replace(/^-|-$/g, "")
		.slice(0, 100);
	return name || "game-chat";
}

async function api(path, options = {}) {
	const response = await fetch(`${apiBase}${path}`, {
		...options,
		headers: {
			authorization: `Bearer ${internalSecret}`,
			"content-type": "application/json",
			...(options.headers ?? {}),
		},
	});
	const body = await response.json().catch(() => ({}));
	return {ok: response.ok, status: response.status, body};
}

async function userStatus(userId) {
	return api(`/internal/discord/status?discord_user_id=${encodeURIComponent(userId)}`, {method: "GET"});
}

function ephemeral(content) {
	return {content, flags: MessageFlags.Ephemeral};
}

const commands = [
	new SlashCommandBuilder()
		.setName("link")
		.setDescription("Link your UClient account"),
	new SlashCommandBuilder()
		.setName("create-message-channel")
		.setDescription("Create a private channel for server chat")
		.addStringOption(option => option
			.setName("name")
			.setDescription("Channel name")
			.setRequired(true)
			.setMaxLength(100)),
	new SlashCommandBuilder()
		.setName("delete-message-channel")
		.setDescription("Delete your server chat channel"),
	new SlashCommandBuilder()
		.setName("send")
		.setDescription("Send a message from your linked UClient account")
		.addStringOption(option => option
			.setName("message")
			.setDescription("Message to send")
			.setRequired(true)
			.setMaxLength(512))
		.addStringOption(option => option
			.setName("mode")
			.setDescription("All, team, or UClient")
			.addChoices(
				{name: "All", value: "all"},
				{name: "Team", value: "team"},
				{name: "UClient", value: "uclient"},
			))
		.addStringOption(option => option
			.setName("room")
			.setDescription("UClient room")
			.setAutocomplete(true)),
].map(command => command.toJSON());

const rest = new REST({version: "10"}).setToken(token);
if(guildId)
	await rest.put(Routes.applicationGuildCommands(applicationId, guildId), {body: commands});
else
	await rest.put(Routes.applicationCommands(applicationId), {body: commands});

const client = new Client({
	intents: [
		GatewayIntentBits.Guilds,
		GatewayIntentBits.GuildMessages,
		GatewayIntentBits.MessageContent,
	],
});

client.on("interactionCreate", async interaction => {
	if(interaction.isAutocomplete()) {
		if(interaction.commandName !== "send" || interaction.options.getFocused(true).name !== "room") {
			await interaction.respond([]).catch(() => {});
			return;
		}
		const query = interaction.options.getFocused().toLowerCase();
		const listed = await api(`/internal/discord/rooms?discord_user_id=${encodeURIComponent(interaction.user.id)}`, {method: "GET"});
		const rooms = Array.isArray(listed.body.rooms) ? listed.body.rooms : [];
		const choices = rooms
			.filter(room => typeof room?.name === "string" && typeof room?.id === "string" && room.name.toLowerCase().includes(query))
			.slice(0, 25)
			.map(room => {
				const suffix = ` (${Number(room.member_count) || 0})`;
				return {
					name: `${room.name.slice(0, 100 - suffix.length)}${suffix}`,
					value: room.id.slice(0, 100),
				};
			});
		await interaction.respond(choices).catch(() => {});
		return;
	}
	if(!interaction.isChatInputCommand())
		return;
	try {
		if(interaction.commandName === "link") {
			const started = await api("/internal/discord/link/start", {
				method: "POST",
				body: JSON.stringify({
					discord_user_id: interaction.user.id,
					application_id: interaction.applicationId,
					interaction_token: interaction.token,
					guild_id: interaction.guildId ?? "",
				}),
			});
			if(!started.ok || !started.body.url) {
				await interaction.reply(ephemeral("Could not start account linking. Try again."));
				return;
			}
			await interaction.reply(ephemeral(`${text.openLink}\n${started.body.url}`));
			return;
		}

		if(!interaction.inGuild()) {
			await interaction.reply(ephemeral(text.guildOnly));
			return;
		}

		if(interaction.commandName === "create-message-channel") {
			const status = await userStatus(interaction.user.id);
			if(!status.body.linked) {
				await interaction.reply(ephemeral(status.body.message || text.notLinked));
				return;
			}
			if(status.body.channel_id) {
				await interaction.reply(ephemeral(text.channelExists));
				return;
			}
			const name = channelName(interaction.options.getString("name", true));
			if(!interaction.guild) {
				await interaction.reply(ephemeral(text.createFailed));
				return;
			}
			let channel;
			try {
				channel = await interaction.guild.channels.create({
					name,
					type: ChannelType.GuildText,
					permissionOverwrites: [
						{id: interaction.guildId, deny: [PermissionFlagsBits.ViewChannel]},
						{
							id: interaction.user.id,
							allow: [
								PermissionFlagsBits.ViewChannel,
								PermissionFlagsBits.SendMessages,
								PermissionFlagsBits.ReadMessageHistory,
								PermissionFlagsBits.ManageChannels,
							],
						},
						{
							id: interaction.client.user.id,
							allow: [
								PermissionFlagsBits.ViewChannel,
								PermissionFlagsBits.SendMessages,
								PermissionFlagsBits.ReadMessageHistory,
								PermissionFlagsBits.ManageChannels,
								PermissionFlagsBits.ManageMessages,
							],
						},
					],
				});
			}
			catch {
				await interaction.reply(ephemeral(text.createFailed));
				return;
			}
			const registered = await api("/internal/discord/channels", {
				method: "POST",
				body: JSON.stringify({
					discord_user_id: interaction.user.id,
					guild_id: interaction.guildId,
					channel_id: channel.id,
				}),
			});
			if(!registered.ok) {
				await channel.delete().catch(() => {});
				await interaction.reply(ephemeral(registered.body.message || text.createFailed));
				return;
			}
			await interaction.reply(ephemeral(text.created));
			return;
		}

		if(interaction.commandName === "delete-message-channel") {
			const removed = await api("/internal/discord/channels", {
				method: "DELETE",
				body: JSON.stringify({discord_user_id: interaction.user.id}),
			});
			if(!removed.ok) {
				await interaction.reply(ephemeral(removed.body.message || text.noChannel));
				return;
			}
			const channel = await client.channels.fetch(removed.body.channel_id).catch(() => null);
			if(channel && "delete" in channel)
				await channel.delete().catch(() => {});
			await interaction.reply(ephemeral(text.deleted));
			return;
		}

		if(interaction.commandName === "send") {
			const message = interaction.options.getString("message", true).trim();
			const mode = interaction.options.getString("mode") ?? "";
			const roomId = interaction.options.getString("room") ?? "";
			if(!message) {
				await interaction.reply(ephemeral(text.emptyMessage));
				return;
			}
			if(roomId && (mode === "all" || mode === "team")) {
				await interaction.reply(ephemeral(text.modeRoomConflict));
				return;
			}
			const status = await userStatus(interaction.user.id);
			if(!status.body.linked) {
				await interaction.reply(ephemeral(status.body.message || text.notLinked));
				return;
			}
			const sent = await api("/internal/discord/send", {
				method: "POST",
				body: JSON.stringify({
					discord_user_id: interaction.user.id,
					content: message,
					mode: roomId && !mode ? "uclient" : (mode || "all"),
					room_id: roomId,
				}),
			});
			if(!sent.body.ok) {
				await interaction.reply(ephemeral(sent.body.message || "Something went wrong. Try again."));
				return;
			}
			if(sent.body.ignored) {
				await interaction.reply(ephemeral(text.emptyMessage));
				return;
			}
			await interaction.reply(ephemeral(text.sent));
			return;
		}
	}
	catch(errorValue) {
		console.error(JSON.stringify({
			event: "discord_command_failed",
			message: errorValue instanceof Error ? errorValue.message : String(errorValue),
		}));
		if(interaction.deferred || interaction.replied)
			return;
		await interaction.reply(ephemeral("Something went wrong. Try again.")).catch(() => {});
	}
});

client.on("messageCreate", async message => {
	if(message.author.bot || !message.guild || !message.channel.isTextBased())
		return;
	const content = message.content.trim();
	if(!content)
		return;
	const result = await api("/internal/discord/inbound", {
		method: "POST",
		body: JSON.stringify({channel_id: message.channel.id, content}),
	});
	if(result.status === 404)
		return;
	if(!result.body.ok && result.body.reason === "offline") {
		await message.channel.send({
			content: result.body.message || text.notInGame,
			allowedMentions: {parse: []},
		}).catch(() => {});
		return;
	}
	if(result.body.ok) {
		const deleted = await message.delete().then(() => true).catch(() => false);
		if(!deleted && "permissionOverwrites" in message.channel) {
			await message.channel.permissionOverwrites.edit(message.client.user.id, {ManageMessages: true}).catch(() => {});
			await message.delete().catch(() => {});
		}
	}
});

client.once("clientReady", () => {
	console.log(JSON.stringify({event: "discord_bot_ready", user: client.user?.tag ?? ""}));
});

await client.login(token);
