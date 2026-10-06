// Env: DISCORD_TOKEN, DISCORD_APPLICATION_ID, DISCORD_INTERNAL_SECRET
// Optional: UCLIENT_API_BASE_URL (default https://uclient.under1111.com), DISCORD_GUILD_ID
// The bot needs the Message Content intent to read channel messages.
import {
	ChannelType,
	Client,
	ActionRowBuilder,
	ButtonBuilder,
	ButtonStyle,
	ContainerBuilder,
	GatewayIntentBits,
	LabelBuilder,
	MessageFlags,
	ModalBuilder,
	PermissionFlagsBits,
	REST,
	Routes,
	SectionBuilder,
	SeparatorBuilder,
	SlashCommandBuilder,
	TextDisplayBuilder,
	TextInputBuilder,
	TextInputStyle,
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
	sent: "The message was sent.",
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

const deferredFlags = MessageFlags.IsComponentsV2 | MessageFlags.Ephemeral;

function panel(blocks) {
	const container = new ContainerBuilder().setAccentColor(0x5865F2);
	blocks.filter(Boolean).slice(0, 12).forEach((block, index) => {
		if(index > 0)
			container.addSeparatorComponents(new SeparatorBuilder().setDivider(true));
		container.addTextDisplayComponents(new TextDisplayBuilder().setContent(String(block).slice(0, 4000)));
	});
	return {
		components: [container],
		flags: MessageFlags.IsComponentsV2,
	};
}

function ephemeral(content) {
	return {...panel([content]), flags: deferredFlags};
}

function serverAddress(group) {
	const explicit = typeof group?.address === "string" ? group.address.trim() : "";
	if(explicit)
		return explicit;
	const title = typeof group?.title === "string" ? group.title.trim() : "";
	return title.split(/\s+/)[0] ?? "";
}

function onlineList(groups) {
	const container = new ContainerBuilder().setAccentColor(0x5865F2);
	container.addTextDisplayComponents(new TextDisplayBuilder().setContent("**Online friends**"));
	for(const group of groups.slice(0, 9)) {
		container.addSeparatorComponents(new SeparatorBuilder().setDivider(true));
		const address = serverAddress(group);
		const customId = `connect:${address}`;
		const section = new SectionBuilder().addTextDisplayComponents(
			new TextDisplayBuilder().setContent(`**${group.title}**\n${group.names}`.slice(0, 4000)),
		);
		if(address && customId.length <= 100) {
			section.setButtonAccessory(
				new ButtonBuilder()
					.setCustomId(customId)
					.setLabel("Connect")
					.setStyle(ButtonStyle.Primary),
			);
		}
		container.addSectionComponents(section);
	}
	return {
		components: [container],
		flags: MessageFlags.IsComponentsV2,
	};
}

function shown(value) {
	const text = String(value ?? "").replace(/[*_`]/g, "").trim();
	return text || "-";
}

function playerCard(query, hits, page) {
	const hit = hits[page] ?? hits[0];
	const container = new ContainerBuilder().setAccentColor(0x5865F2);
	container.addTextDisplayComponents(new TextDisplayBuilder().setContent(`**${shown(hit.player)}**`.slice(0, 4000)));
	container.addSeparatorComponents(new SeparatorBuilder().setDivider(true));
	const count = Number.isFinite(hit.count) ? hit.count : "-";
	const max = Number.isFinite(hit.max) ? hit.max : "-";
	container.addTextDisplayComponents(new TextDisplayBuilder().setContent([
		"### Server info",
		`Name: ${shown(hit.name)}`,
		`Map: ${shown(hit.map)}`,
		`Players: ${count}/${max}`,
		`Password: ${hit.password ? "O" : "X"}`,
	].join("\n")));
	const address = typeof hit.address === "string" ? hit.address.trim() : "";
	const connectId = `connect:${address}`;
	const player = typeof hit.player === "string" ? hit.player.trim() : "";
	const profileUrl = player ? `https://ddnet.org/players/${encodeURIComponent(player)}/` : "";
	const buttons = [];
	if(address && connectId.length <= 100)
		buttons.push(new ButtonBuilder().setCustomId(connectId).setLabel("Connect").setStyle(ButtonStyle.Primary));
	if(profileUrl.length > 0 && profileUrl.length <= 512)
		buttons.push(new ButtonBuilder().setLabel("Search on the official site").setStyle(ButtonStyle.Link).setURL(profileUrl));
	if(buttons.length) {
		container.addSeparatorComponents(new SeparatorBuilder().setDivider(true));
		container.addActionRowComponents(new ActionRowBuilder().addComponents(buttons));
	}
	const components = [container];
	const pageId = `find:0:${query}`;
	if(hits.length > 1 && pageId.length <= 100) {
		components.push(new ActionRowBuilder().addComponents(
			new ButtonBuilder()
				.setCustomId(`find:${page - 1}:${query}`)
				.setLabel("<")
				.setStyle(ButtonStyle.Secondary)
				.setDisabled(page <= 0),
			new ButtonBuilder()
				.setCustomId(`find:${page + 1}:${query}`)
				.setLabel(">")
				.setStyle(ButtonStyle.Secondary)
				.setDisabled(page >= hits.length - 1),
		));
	}
	return {components, flags: MessageFlags.IsComponentsV2};
}

const chatStyles = [
	["style1", "Style 1", "[message]"],
	["style2", "Style 2", "[message] - This message was sent from Discord."],
	["style3", "Style 3", "[message] - This message was sent from Discord by [displayname] ([username])."],
];

function chatSettings(style, template) {
	const current = chatStyles.some(item => item[0] === style) || style === "custom" ? style : "style1";
	const custom = String(template ?? "").trim();
	const container = new ContainerBuilder().setAccentColor(0x5865F2);
	container.addTextDisplayComponents(new TextDisplayBuilder().setContent("Please choose the message style that is sent when a message is delivered."));
	for(const [id, title, preview] of chatStyles) {
		container.addSeparatorComponents(new SeparatorBuilder().setDivider(true));
		container.addTextDisplayComponents(new TextDisplayBuilder().setContent(`### ${title}\n${preview}`));
	}
	container.addSeparatorComponents(new SeparatorBuilder().setDivider(true));
	container.addTextDisplayComponents(new TextDisplayBuilder().setContent(`### Custom\n${custom || "-"}`.slice(0, 4000)));
	return {
		components: [
			container,
			new ActionRowBuilder().addComponents(
				...chatStyles.map(([id, title]) => new ButtonBuilder()
					.setCustomId(`settings:chat:${id}`)
					.setLabel(title)
					.setStyle(ButtonStyle.Secondary)
					.setDisabled(current === id)),
				new ButtonBuilder()
					.setCustomId("settings:chat:custom")
					.setLabel("Custom")
					.setStyle(ButtonStyle.Secondary)
					.setDisabled(current === "custom"),
			),
		],
		flags: MessageFlags.IsComponentsV2,
	};
}

function customMessageModal(template) {
	const input = new TextInputBuilder()
		.setCustomId("template")
		.setStyle(TextInputStyle.Paragraph)
		.setRequired(true)
		.setMaxLength(300)
		.setPlaceholder("Hello [message] [username]");
	const current = String(template ?? "").trim();
	if(current)
		input.setValue(current.slice(0, 300));
	return new ModalBuilder()
		.setCustomId("settings:chat:custom")
		.setTitle("Custom message")
		.addTextDisplayComponents(new TextDisplayBuilder().setContent([
			"Please enter the message that is sent in the game when you send a message from Discord.",
			"[message] - the message you typed",
			"[displayname] - Discord display name",
			"[username] - Discord username",
		].join("\n")))
		.addLabelComponents(new LabelBuilder()
			.setLabel("Message style")
			.setTextInputComponent(input));
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
		.setName("connect")
		.setDescription("Connect to a game server")
		.addStringOption(option => option
			.setName("address")
			.setDescription("Server address as host:port")
			.setRequired(true)
			.setMaxLength(128))
		.addStringOption(option => option
			.setName("password")
			.setDescription("Server password")
			.setMaxLength(128)),
	new SlashCommandBuilder()
		.setName("disconnect")
		.setDescription("Leave the game server"),
	new SlashCommandBuilder()
		.setName("start-game")
		.setDescription("Start the game"),
	new SlashCommandBuilder()
		.setName("stop-game")
		.setDescription("Stop the game"),
	new SlashCommandBuilder()
		.setName("find-player")
		.setDescription("Find which public server a player is on")
		.addStringOption(option => option
			.setName("name")
			.setDescription("Player name")
			.setRequired(true)
			.setMaxLength(64)),
	new SlashCommandBuilder()
		.setName("online-list")
		.setDescription("Show online friends from the launcher"),
	new SlashCommandBuilder()
		.setName("settings")
		.setDescription("Change Discord chat settings")
		.addStringOption(option => option
			.setName("setting")
			.setDescription("Setting to change")
			.setRequired(true)
			.addChoices({name: "Chat message", value: "chat_message"})),
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
	if(interaction.isModalSubmit() && interaction.customId === "settings:chat:custom") {
		const template = interaction.fields.getTextInputValue("template").trim();
		try {
			if(!template) {
				await interaction.reply(ephemeral("Enter a message style."));
				return;
			}
			await interaction.deferUpdate();
			const saved = await api("/internal/discord/settings", {
				method: "POST",
				body: JSON.stringify({
					discord_user_id: interaction.user.id,
					chat_style: "custom",
					chat_template: template,
				}),
			});
			if(!saved.body.linked)
				await interaction.editReply(panel([saved.body.message || text.notLinked]));
			else
				await interaction.editReply(chatSettings(saved.body.chat_style, saved.body.chat_template));
		}
		catch(errorValue) {
			console.error(JSON.stringify({
				event: "discord_command_failed",
				message: errorValue instanceof Error ? errorValue.message : String(errorValue),
			}));
			if(interaction.deferred || interaction.replied)
				await interaction.editReply(panel(["Something went wrong. Try again."])).catch(() => {});
			else
				await interaction.reply(ephemeral("Something went wrong. Try again.")).catch(() => {});
		}
		return;
	}
	if(interaction.isButton() && interaction.customId.startsWith("settings:chat:")) {
		const choice = interaction.customId.slice("settings:chat:".length);
		try {
			if(choice === "custom") {
				const current = await api(`/internal/discord/settings?discord_user_id=${encodeURIComponent(interaction.user.id)}`, {method: "GET"});
				await interaction.showModal(customMessageModal(current.body.chat_template || ""));
				return;
			}
			if(choice !== "style1" && choice !== "style2" && choice !== "style3")
				return;
			await interaction.deferUpdate();
			const saved = await api("/internal/discord/settings", {
				method: "POST",
				body: JSON.stringify({
					discord_user_id: interaction.user.id,
					chat_style: choice,
				}),
			});
			if(!saved.body.linked)
				await interaction.editReply(panel([saved.body.message || text.notLinked]));
			else
				await interaction.editReply(chatSettings(saved.body.chat_style, saved.body.chat_template));
		}
		catch(errorValue) {
			console.error(JSON.stringify({
				event: "discord_command_failed",
				message: errorValue instanceof Error ? errorValue.message : String(errorValue),
			}));
			if(interaction.deferred || interaction.replied)
				await interaction.editReply(panel(["Something went wrong. Try again."])).catch(() => {});
			else
				await interaction.reply(ephemeral("Something went wrong. Try again.")).catch(() => {});
		}
		return;
	}
	if(interaction.isButton() && interaction.customId.startsWith("find:")) {
		const rest = interaction.customId.slice("find:".length);
		const splitAt = rest.indexOf(":");
		const page = Number(rest.slice(0, splitAt));
		const name = splitAt >= 0 ? rest.slice(splitAt + 1) : "";
		try {
			await interaction.deferUpdate();
			const result = await api(`/internal/discord/player-search?name=${encodeURIComponent(name)}`, {method: "GET"});
			const hits = Array.isArray(result.body.hits) ? result.body.hits : [];
			if(!result.body.found || !hits.length) {
				await interaction.editReply(panel([result.body.message || "Something went wrong. Try again."]));
				return;
			}
			const index = Number.isInteger(page) ? Math.min(Math.max(page, 0), hits.length - 1) : 0;
			await interaction.editReply(playerCard(name, hits, index));
		}
		catch(errorValue) {
			console.error(JSON.stringify({
				event: "discord_command_failed",
				message: errorValue instanceof Error ? errorValue.message : String(errorValue),
			}));
			if(interaction.deferred || interaction.replied)
				await interaction.editReply(panel(["Something went wrong. Try again."])).catch(() => {});
		}
		return;
	}
	if(interaction.isButton() && interaction.customId.startsWith("connect:")) {
		const address = interaction.customId.slice("connect:".length);
		try {
			await interaction.deferReply({flags: deferredFlags});
			const result = await api("/internal/discord/connect", {
				method: "POST",
				body: JSON.stringify({
					discord_user_id: interaction.user.id,
					application_id: applicationId,
					interaction_token: interaction.token,
					address,
				}),
			});
			await interaction.editReply(panel([result.body.message || "Something went wrong. Try again."]));
		}
		catch(errorValue) {
			console.error(JSON.stringify({
				event: "discord_command_failed",
				message: errorValue instanceof Error ? errorValue.message : String(errorValue),
			}));
			if(interaction.deferred || interaction.replied)
				await interaction.editReply(panel(["Something went wrong. Try again."])).catch(() => {});
			else
				await interaction.reply(ephemeral("Something went wrong. Try again.")).catch(() => {});
		}
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

		if(interaction.commandName === "find-player") {
			await interaction.deferReply({flags: deferredFlags});
			const name = interaction.options.getString("name", true).trim();
			const result = await api(`/internal/discord/player-search?name=${encodeURIComponent(name)}`, {method: "GET"});
			const hits = Array.isArray(result.body.hits) ? result.body.hits : [];
			if(!result.body.found || !hits.length) {
				await interaction.editReply(panel([result.body.message || "Something went wrong. Try again."]));
				return;
			}
			await interaction.editReply(playerCard(name, hits, 0));
			return;
		}

		if(interaction.commandName === "online-list") {
			await interaction.deferReply({flags: deferredFlags});
			const result = await api(`/internal/discord/online-friends?discord_user_id=${encodeURIComponent(interaction.user.id)}`, {method: "GET"});
			const groups = Array.isArray(result.body.groups) ? result.body.groups : [];
			if(!groups.length) {
				const fallback = result.body.description ? ["**Online friends**", result.body.description] : [result.body.message || "Something went wrong. Try again."];
				await interaction.editReply(panel(fallback));
				return;
			}
		await interaction.editReply(onlineList(groups));
		return;
	}

	if(interaction.commandName === "settings") {
		await interaction.deferReply({flags: deferredFlags});
		const result = await api(`/internal/discord/settings?discord_user_id=${encodeURIComponent(interaction.user.id)}`, {method: "GET"});
		if(!result.body.linked)
			await interaction.editReply(panel([result.body.message || text.notLinked]));
		else
			await interaction.editReply(chatSettings(result.body.chat_style, result.body.chat_template));
		return;
	}

	if(interaction.commandName === "connect" || interaction.commandName === "disconnect" || interaction.commandName === "start-game" || interaction.commandName === "stop-game") {
			await interaction.deferReply({flags: deferredFlags});
			const body = {
				discord_user_id: interaction.user.id,
				application_id: applicationId,
				interaction_token: interaction.token,
			};
			if(interaction.commandName === "connect") {
				body.address = interaction.options.getString("address", true);
				body.password = interaction.options.getString("password") ?? "";
			}
			const result = await api(`/internal/discord/${interaction.commandName}`, {
				method: "POST",
				body: JSON.stringify(body),
			});
			await interaction.editReply(panel([result.body.message || "Something went wrong. Try again."]));
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
						{id: interaction.guildId, deny: [PermissionFlagsBits.ViewChannel, PermissionFlagsBits.SendMessages]},
						{
							id: interaction.user.id,
							allow: [
								PermissionFlagsBits.ViewChannel,
								PermissionFlagsBits.SendMessages,
								PermissionFlagsBits.ReadMessageHistory,
								PermissionFlagsBits.ManageChannels,
								PermissionFlagsBits.ManageRoles,
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
		if(interaction.deferred || interaction.replied) {
			await interaction.editReply(panel(["Something went wrong. Try again."])).catch(() => {});
			return;
		}
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
		body: JSON.stringify({
			channel_id: message.channel.id,
			message_id: message.id,
			content,
			display_name: message.member?.displayName || message.author.globalName || message.author.username,
			username: message.author.username,
		}),
	});
	if(result.status === 404)
		return;
	if(!result.body.ok && result.body.reason === "offline") {
		await message.author.send({
			content: result.body.message || text.notInGame,
			allowedMentions: {parse: []},
		}).catch(() => {});
	}
});

client.once("clientReady", () => {
	console.log(JSON.stringify({event: "discord_bot_ready", user: client.user?.tag ?? ""}));
});

await client.login(token);
